// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_ENTITY_TABLE_H_
#define V8_SANDBOX_EXTERNAL_ENTITY_TABLE_H_

#include "src/common/segmented-table.h"

namespace v8 {
namespace internal {

class Isolate;

/**
 * A thread-safe table with a fixed maximum size for storing references to
 * objects located outside of the sandbox.
 *
 * An external entity table provides the basic mechanisms to ensure
 * safe access to objects located outside the sandbox, but referenced
 * from within it. When an external entity table is used, objects located
 * inside the sandbox reference outside objects through indices into the table.
 *
 * The ExternalEntityTable class should be seen an an incomplete class that
 * needs to be extended by a concrete implementation class, such as the
 * ExternalPointerTable class, as it is lacking some functionality. In
 * particular, while the ExternalEntityTable implements basic table memory
 * management as well as entry allocation routines, it does not implement any
 * logic for reclaiming entries such as garbage collection. This must be done
 * by the child classes.
 *
 * For the purpose of memory management, the table is partitioned into Segments
 * (for example 64kb memory chunks) that are grouped together in "Spaces". All
 * segments in a space share a freelist, and so entry allocation and garbage
 * collection happen on the level of spaces.
 *
 * The Entry type defines how the freelist is represented. For that, it must
 * implement the following methods:
 * - void MakeFreelistEntry(uint32_t next_entry_index)
 * - uint32_t GetNextFreelistEntry()
 */
template <typename Entry, size_t size>
class V8_EXPORT_PRIVATE ExternalEntityTable
    : public SegmentedTable<Entry, size> {
 protected:
  using Base = SegmentedTable<Entry, size>;

  struct Space : public Base::Space {
   public:
    // Whether this space is attached to a table's internal read-only segment.
    bool is_internal_read_only_space() const {
      return is_internal_read_only_space_;
    }

   protected:
    // Whether this is the internal RO space, which has special semantics:
    // - read-only page permissions after initialization,
    // - the space is not swept since slots are live by definition,
    // - contains exactly one segment, located at offset 0, and
    // - the segment's lifecycle is managed by `owning_table_`.
    bool is_internal_read_only_space_ = false;

    friend class ExternalEntityTable<Entry, size>;
  };

  // A Space that supports black allocations.
  struct SpaceWithBlackAllocationSupport : public Space {
    bool allocate_black() { return allocate_black_; }
    void set_allocate_black(bool allocate_black) {
      allocate_black_ = allocate_black;
    }

   private:
    bool allocate_black_ = false;
  };

  // Attempts to allocate an entry in the given space below the specified index.
  //
  // If there are no free entries at a lower index, this method will fail and
  // return zero. This method will therefore never allocate a new segment.
  // This method is atomic and can be called from background threads.
  uint32_t AllocateEntryBelow(Space* space, uint32_t threshold_index);

  // Sweeps the given space.
  //
  // This will free all unmarked entries to the freelist and unmark all live
  // entries. The table is swept top-to-bottom so that the freelist ends up
  // sorted. During sweeping, new entries must not be allocated.
  //
  // This is a generic implementation of table sweeping and requires that the
  // Entry type implements the following additional methods:
  // - bool IsMarked()
  // - void Unmark()
  //
  // Returns the number of live entries after sweeping.
  uint32_t GenericSweep(Space* space);

  // Iterate over all entries in the given space.
  //
  // The callback function will be invoked for every entry and be passed the
  // index of that entry as argument.
  template <typename Callback>
  void IterateEntriesIn(Space* space, Callback callback);

  // Marker value for the freelist_head_ member to indicate that entry
  // allocation is currently forbidden, for example because the table is being
  // swept as part of a mark+sweep garbage collection. This value should never
  // occur as freelist_head_ value during normal operations and should be easy
  // to recognize.
  static constexpr Base::FreelistHead kEntryAllocationIsForbiddenMarker =
      typename Base::FreelistHead(-1, -1);

 public:
  // Initializes the table by reserving the backing memory, allocating an
  // initial segment, and populating the freelist.
  void Initialize();

  // Attaches/detaches the given space to the internal read-only segment. Note
  // the lifetime of the underlying segment itself is managed by the table.
  void AttachSpaceToReadOnlySegment(Space* space);
  void DetachSpaceFromReadOnlySegment(Space* space);

  // Use this scope to temporarily unseal the read-only segment (i.e. change
  // permissions to RW).
  class UnsealReadOnlySegmentScope final {
   public:
    explicit UnsealReadOnlySegmentScope(ExternalEntityTable<Entry, size>* table)
        : table_(table) {
      table_->UnsealReadOnlySegment();
    }

    ~UnsealReadOnlySegmentScope() { table_->SealReadOnlySegment(); }

   private:
    ExternalEntityTable<Entry, size>* const table_;
  };

 private:
  // Required for Isolate::CheckIsolateLayout().
  friend class Isolate;

  static constexpr uint32_t kInternalReadOnlySegmentOffset = 0;
  static constexpr uint32_t kInternalNullEntryIndex = 0;

  // Helpers to toggle the first segment's permissions between kRead (sealed)
  // and kReadWrite (unsealed).
  void UnsealReadOnlySegment();
  void SealReadOnlySegment();
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_EXTERNAL_ENTITY_TABLE_H_
