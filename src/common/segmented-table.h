// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_SEGMENTED_TABLE_H_
#define V8_COMMON_SEGMENTED_TABLE_H_

#include <set>

#include "include/v8-internal.h"
#include "src/base/macros.h"
#include "src/common/code-memory-access.h"

namespace v8 {
namespace internal {

template <typename Entry, size_t size>
class V8_EXPORT_PRIVATE SegmentedTable {
 public:
  // Initializes the table by reserving the backing memory, allocating an
  // initial segment, and populating the freelist.
  V8_INLINE void Initialize();

  // Deallocates all memory associated with this table.
  V8_INLINE void TearDown();

  static constexpr bool IsWriteProtected = Entry::IsWriteProtected;
  static constexpr int kEntrySize = sizeof(Entry);
  static constexpr size_t kReservationSize = size;
  static constexpr size_t kMaxCapacity = kReservationSize / kEntrySize;

  // For managing the table's backing memory, the table is partitioned into
  // segments of this size. Segments can then be allocated and freed using the
  // AllocateTableSegment() and FreeTableSegment() routines.
  static constexpr size_t kSegmentSize = 64 * KB;
  static constexpr size_t kEntriesPerSegment = kSegmentSize / kEntrySize;

  // Struct representing a segment of the table.
  struct Segment {
   public:
    // Initialize a segment given its number.
    explicit Segment(uint32_t number) : number_(number) {}

    // Returns the segment starting at the specified offset from the base of the
    // table.
    static Segment At(uint32_t offset);

    // Returns the segment containing the entry at the given index.
    static Segment Containing(uint32_t entry_index);

    // The segments of a table are numbered sequentially. This method returns
    // the number of this segment.
    uint32_t number() const { return number_; }

    // Returns the offset of this segment from the table base.
    uint32_t offset() const { return number_ * kSegmentSize; }

    // Returns the index of the first entry in this segment.
    uint32_t first_entry() const { return number_ * kEntriesPerSegment; }

    // Return the index of the last entry in this segment.
    uint32_t last_entry() const {
      return first_entry() + kEntriesPerSegment - 1;
    }

    // Segments are ordered by their id/offset.
    bool operator<(const Segment& other) const {
      return number_ < other.number_;
    }

   private:
    // A segment is identified by its number, which is its offset from the base
    // of the table divided by the segment size.
    const uint32_t number_;
  };

  // Struct representing the head of the freelist.
  //
  // An external entity table uses simple, singly-linked lists to manage free
  // entries. Each entry on the freelist contains the 32-bit index of the next
  // entry. The last entry points to zero.
  struct FreelistHead {
    constexpr FreelistHead() : next_(0), length_(0) {}
    constexpr FreelistHead(uint32_t next, uint32_t length)
        : next_(next), length_(length) {}

    // Returns the index of the next entry on the freelist.
    // If the freelist is empty, this returns zero.
    uint32_t next() const { return next_; }

    // Returns the total length of the freelist.
    uint32_t length() const { return length_; }

    bool is_empty() const { return length_ == 0; }

   private:
    uint32_t next_;
    uint32_t length_;
  };

  // We expect the FreelistHead struct to fit into a single atomic word.
  // Otherwise, access to it would be slow.
  static_assert(std::atomic<FreelistHead>::is_always_lock_free);

  // A collection of segments in an external entity table.
  //
  // For the purpose of memory management, a table is partitioned into segments
  // of a fixed size (e.g. 64kb). A Space is a collection of segments that all
  // share the same freelist. As such, entry allocation and freeing (e.g.
  // through garbage collection) all happen on the level of spaces.
  //
  // Spaces allow implementing features such as:
  // * Young generation GC support (a separate space is used for all entries
  //   belonging to the young generation)
  // * Having double-width entries in a table (a dedicated space is used that
  //   contains only double-width entries)
  // * Sharing one table between multiple isolates that perform GC independently
  //   (each Isolate owns one space)
  struct Space {
   public:
    Space() = default;
    Space(const Space&) = delete;
    Space& operator=(const Space&) = delete;
    ~Space();

    // Determines the number of entries currently on the freelist.
    // As entries can be allocated from other threads, the freelist size may
    // have changed by the time this method returns. As such, the returned
    // value should only be treated as an approximation.
    uint32_t freelist_length() const;

    // Returns the current number of segments currently associated with this
    // space.
    // The caller must lock the mutex.
    uint32_t num_segments();

    // Returns whether this space is currently empty.
    // The caller must lock the mutex.
    bool is_empty() { return num_segments() == 0; }

    // Returns the current capacity of this space.
    // The capacity of a space is the total number of entries it can contain.
    // The caller must lock the mutex.
    uint32_t capacity() { return num_segments() * kEntriesPerSegment; }

    // Returns true if this space contains the entry with the given index.
    bool Contains(uint32_t index);

#ifdef DEBUG
    // Check whether this space belongs to the given external entity table.
    bool BelongsTo(const void* table) const { return owning_table_ == table; }
#endif  // DEBUG

   protected:
    friend class SegmentedTable<Entry, size>;

#ifdef DEBUG
    // In debug builds we keep track of which table a space belongs to to be
    // able to insert additional DCHECKs that verify that spaces are always used
    // with the correct table.
    std::atomic<void*> owning_table_ = nullptr;
#endif

    // The freelist used by this space.
    // This contains both the index of the first entry in the freelist and the
    // total length of the freelist as both values need to be updated together
    // in a single atomic operation to stay consistent in the case of concurrent
    // entry allocations.
    std::atomic<FreelistHead> freelist_head_ = FreelistHead();

    // The collection of segments belonging to this space.
    std::set<Segment> segments_;

    // Mutex guarding access to the segments_ set.
    base::Mutex mutex_;
  };

  // Initializes the given space for use with this table.
  void InitializeSpace(Space* space);

  // Deallocates all segments owned by the given space.
  void TearDownSpace(Space* space);

 protected:
  SegmentedTable() = default;
  SegmentedTable(const SegmentedTable&) = delete;
  SegmentedTable& operator=(const SegmentedTable&) = delete;

  // This Iterator also acts as a scope object to temporarily lift any
  // write-protection (if IsWriteProtected is true).
  class WriteIterator {
   public:
    explicit WriteIterator(Entry* base, uint32_t index);

    uint32_t index() const { return index_; }
    Entry* operator->() { return &base_[index_]; }
    Entry& operator*() { return base_[index_]; }
    WriteIterator& operator++() {
      index_++;
      DCHECK_LT(index_, size);
      return *this;
    }
    WriteIterator& operator--() {
      DCHECK_GT(index_, 0);
      index_--;
      return *this;
    }

   private:
    Entry* base_;
    uint32_t index_;
    std::conditional_t<IsWriteProtected, CFIMetadataWriteScope,
                       NopRwxMemoryWriteScope>
        write_scope_;
  };

  // Access the entry at the specified index.
  V8_INLINE Entry& at(uint32_t index);
  V8_INLINE const Entry& at(uint32_t index) const;

  // Returns an iterator that can be used to perform multiple write operations
  // without switching the write-protections all the time (if IsWriteProtected
  // is true).
  V8_INLINE WriteIterator iter_at(uint32_t index);

  // Returns true if this table has been initialized.
  V8_INLINE bool is_initialized() const;

  // Allocate a new segment in this table.
  //
  // The segment is initialized with freelist entries.
  V8_INLINE std::pair<Segment, FreelistHead> AllocateTableSegment();

  // Free the specified segment of this table.
  //
  // The memory of this segment will afterwards be inaccessible.
  V8_INLINE void FreeTableSegment(Segment segment);

  // Allocates a new entry in the given space and return its index.
  //
  // If there are no free entries, then this will extend the space by
  // allocating a new segment.
  // This method is atomic and can be called from background threads.
  // V8_INLINE uint32_t AllocateEntry(std::atomic<FreelistHead>*
  // freelist_head_);

  // Returns the base address of this table.
  Address base() const;

  // Allocates a new entry in the given space and return its index.
  //
  // If there are no free entries, then this will extend the space by
  // allocating a new segment.
  // This method is atomic and can be called from background threads.
  uint32_t AllocateEntry(Space* space);

  void FreeEntry(std::atomic<FreelistHead>* freelist_head, uint32_t index);
  FreelistHead LinkFreelist(std::atomic<FreelistHead>* freelist_head,
                            FreelistHead new_freelist, uint32_t last_element);

  // Try to allocate the first entry of the freelist.
  //
  // This method is mostly a wrapper around an atomic compare-and-swap which
  // replaces the current freelist head with the next entry in the freelist,
  // thereby allocating the entry at the start of the freelist.
  // bool TryAllocateEntryFromFreelist(std::atomic<FreelistHead>* freelist_head,
  //                                   FreelistHead freelist);
  bool TryAllocateEntryFromFreelist(std::atomic<FreelistHead>* freelist_head,
                                    uint32_t* handle);

  // Not atomic and should only be used if you have exclusive access to the
  // freelist.
  uint32_t AllocateEntryFromFreelist(FreelistHead* freelist_head);

  // The pointer to the base of the virtual address space backing this table.
  // All entry accesses happen through this pointer.
  // It is equivalent to |vas_->base()| and is effectively const after
  // initialization since the backing memory is never reallocated.
  Entry* base_ = nullptr;

  // The virtual address space backing this table.
  // This is used to manage the underlying OS pages, in particular to allocate
  // and free the segments that make up the table.
  VirtualAddressSpace* vas_ = nullptr;
};

// template <typename Entry, bool concurrent_segment_free>
// class MyTable {
//  public:
//   using Handle = uint32_t;
//   // Segments are units that can be mapped / unmapped.
//   struct Segment {};
//   struct FreelistHead {
//     FreelistHead(Handle entry, uint32_t length);
//   };

//   void Initialize();
//   void TearDown();

//  protected:
//   std::pair<Segment, FreelistHead> AllocateSegment();
//   void FreeSegment(Segment segment);

//   Entry& at(Handle handle);

//   bool TryAllocateFromFreelist(std::atomic<FreelistHead>* freelist_head,
//                                Handle* handle);
//   Handle AllocateFromFreelist(FreelistHead freelist_head);

//   FreelistHead FreeEntry(std::atomic<FreelistHead>* freelist_head,
//                          Handle entry) {
//     return LinkFreelist(freelist_head, FreelistHead(entry, 1), entry);
//   }

//   FreelistHead LinkFreelist(std::atomic<FreelistHead>* freelist_head,
//                             FreelistHead new_freelist, Handle last_element);

//  private:
//   VirtualAddressSpace* vas_ = nullptr;
// #ifdef ON32BIT
//   // on 64 bit, we can have a linear mapping and segment => address lookup is
//   // free.
//   // On 32 bit, we don't want to reserve too much address space and lookup
//   needs
//   // to go through a map.
//   base::Mutex segment_map_mutex_;
//   std::map<Segment, Address> segment_map_;
// #endif
// };

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_SEGMENTED_TABLE_H_
