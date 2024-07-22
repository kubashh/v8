// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_SEGMENTED_TABLE_INL_H_
#define V8_COMMON_SEGMENTED_TABLE_INL_H_

#include "src/base/emulated-virtual-address-subspace.h"
#include "src/common/assert-scope.h"
#include "src/common/segmented-table.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

template <typename Entry, size_t size>
SegmentedTable<Entry, size>::Space::~Space() {
  // The segments belonging to this space must have already been deallocated
  // (through TearDownSpace()), otherwise we may leak memory.
  DCHECK(segments_.empty());
}

template <typename Entry, size_t size>
uint32_t SegmentedTable<Entry, size>::Space::freelist_length() const {
  auto freelist = freelist_head_.load(std::memory_order_relaxed);
  return freelist.length();
}

template <typename Entry, size_t size>
uint32_t SegmentedTable<Entry, size>::Space::num_segments() {
  mutex_.AssertHeld();
  return static_cast<uint32_t>(segments_.size());
}

template <typename Entry, size_t size>
bool SegmentedTable<Entry, size>::Space::Contains(uint32_t index) {
  base::MutexGuard guard(&mutex_);
  Segment segment = Segment::Containing(index);
  return segments_.find(segment) != segments_.end();
}

template <typename Entry, size_t size>
void SegmentedTable<Entry, size>::InitializeSpace(Space* space) {
#ifdef DEBUG
  DCHECK_EQ(space->owning_table_, nullptr);
  space->owning_table_ = this;
#endif
}

template <typename Entry, size_t size>
void SegmentedTable<Entry, size>::TearDownSpace(Space* space) {
  DCHECK(this->is_initialized());
  DCHECK(space->BelongsTo(this));
  for (auto segment : space->segments_) {
    this->FreeTableSegment(segment);
  }
  space->segments_.clear();
}

template <typename Entry, size_t size>
typename SegmentedTable<Entry, size>::Segment
SegmentedTable<Entry, size>::Segment::At(uint32_t offset) {
  DCHECK(IsAligned(offset, kSegmentSize));
  uint32_t number = offset / kSegmentSize;
  return Segment(number);
}

template <typename Entry, size_t size>
typename SegmentedTable<Entry, size>::Segment
SegmentedTable<Entry, size>::Segment::Containing(uint32_t entry_index) {
  uint32_t number = entry_index / kEntriesPerSegment;
  return Segment(number);
}

template <typename Entry, size_t size>
Entry& SegmentedTable<Entry, size>::at(uint32_t index) {
  // all the base_ usages need to change for 32bit
  return base_[index];
}

template <typename Entry, size_t size>
const Entry& SegmentedTable<Entry, size>::at(uint32_t index) const {
  return base_[index];
}

template <typename Entry, size_t size>
typename SegmentedTable<Entry, size>::WriteIterator
SegmentedTable<Entry, size>::iter_at(uint32_t index) {
  return WriteIterator(base_, index);
}

template <typename Entry, size_t size>
bool SegmentedTable<Entry, size>::is_initialized() const {
  DCHECK(!base_ || reinterpret_cast<Address>(base_) == vas_->base());
  return base_ != nullptr;
}

template <typename Entry, size_t size>
Address SegmentedTable<Entry, size>::base() const {
  DCHECK(is_initialized());
  return reinterpret_cast<Address>(base_);
}

template <typename Entry, size_t size>
void SegmentedTable<Entry, size>::Initialize() {
  DCHECK(!is_initialized());
  DCHECK_EQ(vas_, nullptr);

  VirtualAddressSpace* root_space = GetPlatformVirtualAddressSpace();
  DCHECK(IsAligned(kReservationSize, root_space->allocation_granularity()));

  if (root_space->CanAllocateSubspaces()) {
    auto subspace = root_space->AllocateSubspace(VirtualAddressSpace::kNoHint,
                                                 kReservationSize, kSegmentSize,
                                                 PagePermissions::kReadWrite);
    vas_ = subspace.release();
  } else {
    // This may be required on old Windows versions that don't support
    // VirtualAlloc2, which is required for subspaces. In that case, just use a
    // fully-backed emulated subspace.
    Address reservation_base = root_space->AllocatePages(
        VirtualAddressSpace::kNoHint, kReservationSize, kSegmentSize,
        PagePermissions::kNoAccess);
    if (reservation_base) {
      vas_ = new base::EmulatedVirtualAddressSubspace(
          root_space, reservation_base, kReservationSize, kReservationSize);
    }
  }
  if (!vas_) {
    V8::FatalProcessOutOfMemory(
        nullptr, "SegmentedTable::InitializeTable (subspace allocation)");
  }
  base_ = reinterpret_cast<Entry*>(vas_->base());
}

template <typename Entry, size_t size>
void SegmentedTable<Entry, size>::TearDown() {
  DCHECK(is_initialized());

  // Deallocate the (read-only) first segment.
  vas_->FreePages(vas_->base(), kSegmentSize);

  base_ = nullptr;
  delete vas_;
  vas_ = nullptr;
}

template <typename Entry, size_t size>
std::pair<typename SegmentedTable<Entry, size>::Segment,
          typename SegmentedTable<Entry, size>::FreelistHead>
SegmentedTable<Entry, size>::AllocateTableSegment() {
  Address start =
      vas_->AllocatePages(VirtualAddressSpace::kNoHint, kSegmentSize,
                          kSegmentSize, PagePermissions::kReadWrite);
  if (!start) {
    V8::FatalProcessOutOfMemory(nullptr, "SegmentedTable::AllocateSegment");
  }
  uint32_t offset = static_cast<uint32_t>((start - vas_->base()));
  Segment segment = Segment::At(offset);

  uint32_t first = segment.first_entry();
  uint32_t last = segment.last_entry();
  {
    WriteIterator it = iter_at(first);
    while (it.index() != last) {
      it->MakeFreelistEntry(it.index() + 1);
      ++it;
    }
    it->MakeFreelistEntry(0);
  }

  return {segment, FreelistHead(first, kEntriesPerSegment)};
}

template <typename Entry, size_t size>
void SegmentedTable<Entry, size>::FreeTableSegment(Segment segment) {
  Address segment_start = vas_->base() + segment.offset();
  vas_->FreePages(segment_start, kSegmentSize);
}

template <typename Entry, size_t size>
uint32_t SegmentedTable<Entry, size>::AllocateEntry(Space* space) {
  DCHECK(this->is_initialized());
  DCHECK(space->BelongsTo(this));

  // We currently don't want entry allocation to trigger garbage collection as
  // this may cause seemingly harmless pointer field assignments to trigger
  // garbage collection. This is especially true for lazily-initialized
  // external pointer slots which will typically only allocate the external
  // pointer table entry when the pointer is first set to a non-null value.
  DisallowGarbageCollection no_gc;

  uint32_t allocated_entry;
  if (TryAllocateEntryFromFreelist(&space->freelist_head_, &allocated_entry)) {
    return allocated_entry;
  }

  auto segment_and_freelist = AllocateTableSegment();
  Segment segment = segment_and_freelist.first;
  FreelistHead freelist = segment_and_freelist.second;
  allocated_entry = AllocateEntryFromFreelist(&freelist);
  {
    base::MutexGuard guard(&space->mutex_);
    space->segments_.insert(segment);
    LinkFreelist(&space->freelist_head_, freelist, segment.last_entry());
  }

  DCHECK(space->Contains(allocated_entry));
  return allocated_entry;
}

template <typename Entry, size_t size>
bool SegmentedTable<Entry, size>::TryAllocateEntryFromFreelist(
    std::atomic<FreelistHead>* freelist_head, uint32_t* handle) {
  FreelistHead current_head = freelist_head->load();
  if (current_head.is_empty()) {
    return false;
  }

  *handle = current_head.next();
  Entry& freelist_entry = at(*handle);
  // TODO(sroettger): we can't access the entry here if we didn't unlink it
  // first.
  uint32_t next_freelist_entry = freelist_entry.GetNextFreelistEntryIndex();
  FreelistHead new_freelist(next_freelist_entry, current_head.length() - 1);
  bool success = freelist_head->compare_exchange_strong(
      current_head, new_freelist, std::memory_order_relaxed);

  // When the CAS succeeded, the entry must've been a freelist entry.
  // Otherwise, this is not guaranteed as another thread may have allocated
  // and overwritten the same entry in the meantime.
  if (success) {
    DCHECK_IMPLIES(current_head.length() > 1, !new_freelist.is_empty());
    DCHECK_IMPLIES(current_head.length() == 1, new_freelist.is_empty());
  }

  return success;
}

template <typename Entry, size_t size>
uint32_t SegmentedTable<Entry, size>::AllocateEntryFromFreelist(
    FreelistHead* freelist_head) {
  DCHECK(!freelist_head->is_empty());
  uint32_t handle = freelist_head->next();
  uint32_t next_next = at(freelist_head->next()).GetNextFreelistEntryIndex();
  *freelist_head = FreelistHead(next_next, freelist_head->length() - 1);
  return handle;
}

template <typename Entry, size_t size>
void SegmentedTable<Entry, size>::FreeEntry(
    std::atomic<FreelistHead>* freelist_head, uint32_t entry) {
  LinkFreelist(freelist_head, FreelistHead(entry, 1), entry);
}

template <typename Entry, size_t size>
typename SegmentedTable<Entry, size>::FreelistHead
SegmentedTable<Entry, size>::LinkFreelist(
    std::atomic<FreelistHead>* freelist_head, FreelistHead freelist_to_link,
    uint32_t last_element) {
  FreelistHead current_head, new_head;
  do {
    current_head = freelist_head->load();
    new_head = FreelistHead(freelist_to_link.next(),
                            freelist_to_link.length() + current_head.length());
    {
      std::conditional_t<IsWriteProtected, CFIMetadataWriteScope,
                         NopRwxMemoryWriteScope>
          write_scope("write free list entry");
      at(last_element).MakeFreelistEntry(current_head.next());
    }
    // This must be a release store to prevent reordering of the preceeding
    // stores to the freelist from being reordered past this store. See
    // AllocateEntry() for more details.
  } while (!freelist_head->compare_exchange_strong(current_head, new_head,
                                                   std::memory_order_release));

  return new_head;
}

template <typename Entry, size_t size>
SegmentedTable<Entry, size>::WriteIterator::WriteIterator(Entry* base,
                                                          uint32_t index)
    : base_(base), index_(index), write_scope_("pointer table write") {}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_SEGMENTED_TABLE_INL_H_
