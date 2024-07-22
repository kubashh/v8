// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_ENTITY_TABLE_INL_H_
#define V8_SANDBOX_EXTERNAL_ENTITY_TABLE_INL_H_

#include "src/base/iterator.h"
#include "src/common/assert-scope.h"
#include "src/common/segmented-table-inl.h"
#include "src/sandbox/external-entity-table.h"

namespace v8 {
namespace internal {

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::AttachSpaceToReadOnlySegment(
    Space* space) {
  DCHECK(Base::is_initialized());
  DCHECK(space->BelongsTo(this));

  DCHECK(!space->is_internal_read_only_space());
  space->is_internal_read_only_space_ = true;

  UnsealReadOnlySegmentScope unseal_scope(this);

  // Physically attach the segment.
  typename Base::FreelistHead freelist;
  {
    base::MutexGuard guard(&space->mutex_);
    DCHECK_EQ(space->segments_.size(), 0);
    typename Base::Segment segment =
        Base::Segment::At(kInternalReadOnlySegmentOffset);
    space->segments_.insert(segment);
    DCHECK_EQ(space->is_internal_read_only_space(), segment.number() == 0);
    DCHECK_EQ(space->is_internal_read_only_space(),
              segment.offset() == kInternalReadOnlySegmentOffset);

    // Refill the freelist with the entries in the newly allocated segment.
    uint32_t first = segment.first_entry();
    uint32_t last = segment.last_entry();
    // For the internal read-only segment, index 0 is reserved for the `null`
    // entry. The underlying memory has been nulled by allocation, and is
    // therefore already initialized.
#ifdef DEBUG
    CHECK_EQ(first, kInternalNullEntryIndex);
    static constexpr uint8_t kNullBytes[Base::kEntrySize] = {0};
    CHECK_EQ(memcmp(&this->at(first), kNullBytes, Base::kEntrySize), 0);
#endif  // DEBUG
    first = kInternalNullEntryIndex + 1;

    {
      typename Base::WriteIterator it = this->iter_at(first);
      while (it.index() != last) {
        it->MakeFreelistEntry(it.index() + 1);
        ++it;
      }
      it->MakeFreelistEntry(0);
    }

    freelist = this->LinkFreelist(
        &space->freelist_head_,
        typename Base::FreelistHead(first, last - first + 1), last);
  }

  DCHECK(!freelist.is_empty());
  DCHECK_EQ(freelist.next(), kInternalNullEntryIndex + 1);
  DCHECK(space->Contains(freelist.next()));
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::Initialize() {
  Base::Initialize();
  // Allocate the read-only segment of the table. This segment is always
  // located at offset 0, and contains the null entry (pointing at
  // kNullAddress) at index 0. It may later be temporarily marked read-write,
  // see UnsealedReadOnlySegmentScope.
  Address first_segment =
      this->vas_->AllocatePages(this->vas_->base(), Base::kSegmentSize,
                                Base::kSegmentSize, PagePermissions::kRead);
  if (first_segment != this->vas_->base()) {
    V8::FatalProcessOutOfMemory(
        nullptr, "SegmentedTable::InitializeTable (first segment allocation)");
  }
  DCHECK_EQ(first_segment - this->vas_->base(), kInternalReadOnlySegmentOffset);
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::DetachSpaceFromReadOnlySegment(
    Space* space) {
  DCHECK(this->is_initialized());
  DCHECK(space->BelongsTo(this));
  // Remove the RO segment from the space's segment list without freeing it.
  // The table itself manages the RO segment's lifecycle.
  base::MutexGuard guard(&space->mutex_);
  DCHECK_EQ(space->segments_.size(), 1);
  space->segments_.clear();
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::UnsealReadOnlySegment() {
  DCHECK(this->is_initialized());
  bool success = this->vas_->SetPagePermissions(
      this->vas_->base(), Base::kSegmentSize, PagePermissions::kReadWrite);
  CHECK(success);
}

template <typename Entry, size_t size>
void ExternalEntityTable<Entry, size>::SealReadOnlySegment() {
  DCHECK(this->is_initialized());
  bool success = this->vas_->SetPagePermissions(
      this->vas_->base(), Base::kSegmentSize, PagePermissions::kRead);
  CHECK(success);
}

template <typename Entry, size_t size>
uint32_t ExternalEntityTable<Entry, size>::AllocateEntryBelow(
    Space* space, uint32_t threshold_index) {
  DCHECK(this->is_initialized());

  uint32_t allocated_entry;
  if (!Base::TryAllocateEntryFromFreelist(&space->freelist_head_,
                                          &allocated_entry)) {
    return 0;
  }
  if (allocated_entry >= threshold_index) {
    Base::FreeEntry(&space->freelist_head_, allocated_entry);
    return 0;
  }

  DCHECK(space->Contains(allocated_entry));
  DCHECK_NE(allocated_entry, 0);
  DCHECK_LT(allocated_entry, threshold_index);
  return allocated_entry;
}

template <typename Entry, size_t size>
uint32_t ExternalEntityTable<Entry, size>::GenericSweep(Space* space) {
  DCHECK(space->BelongsTo(this));

  // Lock the space. Technically this is not necessary since no other thread can
  // allocate entries at this point, but some of the methods we call on the
  // space assert that the lock is held.
  base::MutexGuard guard(&space->mutex_);

  // There must not be any entry allocations while the table is being swept as
  // that would not be safe. Set the freelist to this special marker value to
  // easily catch any violation of this requirement.
  space->freelist_head_.store(kEntryAllocationIsForbiddenMarker,
                              std::memory_order_relaxed);

  // Here we can iterate over the segments collection without taking a lock
  // because no other thread can currently allocate entries in this space.
  uint32_t current_freelist_head = 0;
  uint32_t current_freelist_length = 0;
  std::vector<typename Base::Segment> segments_to_deallocate;

  for (auto segment : base::Reversed(space->segments_)) {
    // Remember the state of the freelist before this segment in case this
    // segment turns out to be completely empty and we deallocate it.
    uint32_t previous_freelist_head = current_freelist_head;
    uint32_t previous_freelist_length = current_freelist_length;

    // Process every entry in this segment, again going top to bottom.
    for (typename Base::WriteIterator it = this->iter_at(segment.last_entry());
         it.index() >= segment.first_entry(); --it) {
      if (!it->IsMarked()) {
        it->MakeFreelistEntry(current_freelist_head);
        current_freelist_head = it.index();
        current_freelist_length++;
      } else {
        it->Unmark();
      }
    }

    // If a segment is completely empty, free it.
    uint32_t free_entries = current_freelist_length - previous_freelist_length;
    bool segment_is_empty = free_entries == Base::kEntriesPerSegment;
    if (segment_is_empty) {
      segments_to_deallocate.push_back(segment);
      // Restore the state of the freelist before this segment.
      current_freelist_head = previous_freelist_head;
      current_freelist_length = previous_freelist_length;
    }
  }

  // We cannot remove the segments while iterating over the segments set, so
  // defer that until now.
  for (auto segment : segments_to_deallocate) {
    // Segment zero is reserved.
    DCHECK_NE(segment.number(), 0);
    this->FreeTableSegment(segment);
    space->segments_.erase(segment);
  }

  typename Base::FreelistHead new_freelist(current_freelist_head,
                                           current_freelist_length);
  space->freelist_head_.store(new_freelist, std::memory_order_release);
  DCHECK_EQ(space->freelist_length(), current_freelist_length);

  uint32_t num_live_entries = space->capacity() - current_freelist_length;
  return num_live_entries;
}

template <typename Entry, size_t size>
template <typename Callback>
void ExternalEntityTable<Entry, size>::IterateEntriesIn(Space* space,
                                                        Callback callback) {
  DCHECK(space->BelongsTo(this));

  base::MutexGuard guard(&space->mutex_);
  for (auto segment : space->segments_) {
    for (uint32_t i = segment.first_entry(); i <= segment.last_entry(); i++) {
      callback(i);
    }
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_EXTERNAL_ENTITY_TABLE_INL_H_
