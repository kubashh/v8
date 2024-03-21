// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_COMPACTIBLE_EXTERNAL_ENTITY_TABLE_INL_H_
#define V8_SANDBOX_COMPACTIBLE_EXTERNAL_ENTITY_TABLE_INL_H_

#include "src/logging/counters.h"
#include "src/sandbox/compactible-external-entity-table.h"
#include "src/sandbox/external-entity-table-inl.h"
#include "src/sandbox/external-pointer.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry, size>::Mark(
    Space* space, ExternalPointerHandle handle, Address handle_location) {
  DCHECK(space->BelongsTo(this));

  // The handle_location must always contain the given handle. Except:
  // - If the slot is lazily-initialized, the handle may transition from the
  //   null handle to a valid handle. In that case, we'll return from this
  //   function early (see below), which is fine since the newly-allocated
  //   entry will already have been marked as alive during allocation.
  // - If the slot is de-initialized, i.e. reset to the null handle. In that
  //   case, we'll still mark the old entry as alive and potentially mark it for
  //   evacuation. Both of these things are fine though: the entry is just kept
  //   alive a little longer and compaction will detect that the slot has been
  //   de-initialized and not perform the evacuation.
#ifdef DEBUG
  ExternalPointerHandle current_handle = base::AsAtomic32::Acquire_Load(
      reinterpret_cast<ExternalPointerHandle*>(handle_location));
  DCHECK(handle == kNullExternalPointerHandle ||
         current_handle == kNullExternalPointerHandle ||
         handle == current_handle);
#endif

  // The null entry is immortal and immutable, so no need to mark it as alive.
  if (handle == kNullExternalPointerHandle) return;

  uint32_t index = HandleToIndex(handle);
  DCHECK(space->Contains(index));

  // If the table is being compacted and the entry is inside the evacuation
  // area, then allocate and set up an evacuation entry for it.
  MaybeCreateEvacuationEntry(space, index, handle_location);

  // Even if the entry is marked for evacuation, it still needs to be marked as
  // alive as it may be visited during sweeping before being evacuation.
  ExternalEntityTable<Entry, size>::at(index).Mark();
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry, size>::MaybeCreateEvacuationEntry(
    Space* space, uint32_t index, Address handle_location) {
  // Check if the entry should be evacuated for table compaction.
  // The current value of the start of the evacuation area is cached in a local
  // variable here as it otherwise may be changed by another marking thread
  // while this method runs, causing non-optimal behaviour (for example, the
  // allocation of an evacuation entry _after_ the entry that is evacuated).
  uint32_t start_of_evacuation_area =
      space->start_of_evacuation_area_.load(std::memory_order_relaxed);
  if (index >= start_of_evacuation_area) {
    DCHECK(space->IsCompacting());
    uint32_t new_index = ExternalEntityTable<Entry, size>::AllocateEntryBelow(
        space, start_of_evacuation_area);
    if (new_index) {
      DCHECK_LT(new_index, start_of_evacuation_area);
      DCHECK(space->Contains(new_index));
      // Even though the new entry will only be accessed during sweeping, this
      // still needs to be an atomic write as another thread may attempt (and
      // fail) to allocate the same table entry, thereby causing a read from
      // this memory location. Without an atomic store here, TSan would then
      // complain about a data race.
      ExternalEntityTable<Entry, size>::at(new_index).MakeEvacuationEntry(
          handle_location);
    } else {
      // In this case, the application has allocated a sufficiently large
      // number of entries from the freelist so that new entries would now be
      // allocated inside the area that is being compacted. While it would be
      // possible to shrink that area and continue compacting, we probably do
      // not want to put more pressure on the freelist and so instead simply
      // abort compaction here. Entries that have already been visited will
      // still be compacted during Sweep, but there is no guarantee that any
      // blocks at the end of the table will now be completely free.
      space->AbortCompacting(start_of_evacuation_area);
    }
  }
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry, size>::Space::StartCompacting(
    uint32_t start_of_evacuation_area) {
  DCHECK_EQ(invalidated_fields_.size(), 0);
  start_of_evacuation_area_.store(start_of_evacuation_area,
                                  std::memory_order_relaxed);
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry, size>::Space::StopCompacting() {
  start_of_evacuation_area_.store(kNotCompactingMarker,
                                  std::memory_order_relaxed);
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry, size>::Space::AbortCompacting(
    uint32_t start_of_evacuation_area) {
  uint32_t compaction_aborted_marker =
      start_of_evacuation_area | kCompactionAbortedMarker;
  DCHECK_NE(compaction_aborted_marker, kNotCompactingMarker);
  start_of_evacuation_area_.store(compaction_aborted_marker,
                                  std::memory_order_relaxed);
}

template <typename Entry, size_t size>
bool CompactibleExternalEntityTable<Entry, size>::Space::IsCompacting() {
  return start_of_evacuation_area_.load(std::memory_order_relaxed) !=
         kNotCompactingMarker;
}

template <typename Entry, size_t size>
bool CompactibleExternalEntityTable<Entry,
                                    size>::Space::CompactingWasAborted() {
  auto value = start_of_evacuation_area_.load(std::memory_order_relaxed);
  return (value & kCompactionAbortedMarker) == kCompactionAbortedMarker;
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry, size>::Space::
    NotifyExternalPointerFieldInvalidated(Address field_address) {
#ifdef DEBUG
  ExternalPointerHandle handle = base::AsAtomic32::Acquire_Load(
      reinterpret_cast<ExternalPointerHandle*>(field_address));
  DCHECK(this->Contains(HandleToIndex(handle)));
#endif
  if (IsCompacting()) {
    base::MutexGuard guard(&invalidated_fields_mutex_);
    invalidated_fields_.push_back(field_address);
  }
}

template <typename Entry, size_t size>
bool CompactibleExternalEntityTable<Entry, size>::Space::FieldWasInvalidated(
    Address field_address) const {
  invalidated_fields_mutex_.AssertHeld();
  return std::find(invalidated_fields_.begin(), invalidated_fields_.end(),
                   field_address) != invalidated_fields_.end();
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry,
                                    size>::Space::ClearInvalidatedFields() {
  invalidated_fields_mutex_.AssertHeld();
  invalidated_fields_.clear();
}

template <typename Entry, size_t size>
uint32_t CompactibleExternalEntityTable<Entry, size>::SweepAndCompact(
    Space* space, Counters* counters) {
  DCHECK(space->BelongsTo(this));
  DCHECK(!space->is_internal_read_only_space());

  // Lock the space. Technically this is not necessary since no other thread can
  // allocate entries at this point, but some of the methods we call on the
  // space assert that the lock is held.
  base::MutexGuard guard(&space->mutex_);
  // Same for the invalidated fields mutex.
  base::MutexGuard invalidated_fields_guard(&space->invalidated_fields_mutex_);

  // There must not be any entry allocations while the table is being swept as
  // that would not be safe. Set the freelist to this special marker value to
  // easily catch any violation of this requirement.
  space->freelist_head_.store(
      ExternalEntityTable<Entry, size>::kEntryAllocationIsForbiddenMarker,
      std::memory_order_relaxed);

  // When compacting, we can compute the number of unused segments at the end of
  // the table and skip those during sweeping.
  uint32_t start_of_evacuation_area =
      space->start_of_evacuation_area_.load(std::memory_order_relaxed);
  bool evacuation_was_successful = false;
  if (space->IsCompacting()) {
    TableCompactionOutcome outcome;
    if (space->CompactingWasAborted()) {
      // Compaction was aborted during marking because the freelist grew to
      // short. In this case, it is not guaranteed that any segments will now
      // be completely free.
      outcome = TableCompactionOutcome::kAborted;
      // Extract the original start_of_evacuation_area value so that the
      // DCHECKs below and in TryResolveEvacuationEntryDuringSweeping work.
      start_of_evacuation_area &= ~Space::kCompactionAbortedMarker;
    } else {
      // Entry evacuation was successful so all segments inside the evacuation
      // area are now guaranteed to be free and so can be deallocated.
      outcome = TableCompactionOutcome::kSuccess;
      evacuation_was_successful = true;
    }
    DCHECK(IsAligned(start_of_evacuation_area,
                     ExternalEntityTable<Entry, size>::kEntriesPerSegment));

    space->StopCompacting();

    counters->external_pointer_table_compaction_outcome()->AddSample(
        static_cast<int>(outcome));
  }

  // Sweep top to bottom and rebuild the freelist from newly dead and
  // previously freed entries while also clearing the marking bit on live
  // entries and resolving evacuation entries table when compacting the table.
  // This way, the freelist ends up sorted by index which already makes the
  // table somewhat self-compacting and is required for the compaction
  // algorithm so that evacuated entries are evacuated to the start of a space.
  // This method must run either on the mutator thread or while the mutator is
  // stopped.
  uint32_t current_freelist_head = 0;
  uint32_t current_freelist_length = 0;
  auto AddToFreelist = [&](uint32_t entry_index) {
    ExternalEntityTable<Entry, size>::at(entry_index)
        .MakeFreelistEntry(current_freelist_head);
    current_freelist_head = entry_index;
    current_freelist_length++;
  };

  std::vector<typename ExternalEntityTable<Entry, size>::Segment>
      segments_to_deallocate;
  for (auto segment : base::Reversed(space->segments_)) {
    // If we evacuated all live entries in this segment then we can skip it
    // here and directly deallocate it after this loop.
    if (evacuation_was_successful &&
        segment.first_entry() >= start_of_evacuation_area) {
      segments_to_deallocate.push_back(segment);
      continue;
    }

    // Remember the state of the freelist before this segment in case this
    // segment turns out to be completely empty and we deallocate it.
    uint32_t previous_freelist_head = current_freelist_head;
    uint32_t previous_freelist_length = current_freelist_length;

    // Process every entry in this segment, again going top to bottom.
    for (uint32_t i = segment.last_entry(); i >= segment.first_entry(); i--) {
      auto payload = ExternalEntityTable<Entry, size>::at(i).GetRawPayload();
      if (payload.ContainsEvacuationEntry()) {
        bool entry_was_resolved = false;
        // Resolve the evacuation entry: take the pointer to the handle from the
        // evacuation entry, copy the entry to its new location, and finally
        // update the handle to point to the new entry.
        // While we now know that the entry being evacuated is free, we don't
        // add it to (the start of) the freelist because that would immediately
        // cause new fragmentation when the next entry is allocated. Instead, we
        // assume that the segments out of which entries are evacuated will all
        // be decommitted anyway after this loop, which is usually the case
        // unless compaction was already aborted during marking.
        Address handle_location =
            payload.ExtractEvacuationEntryHandleLocation();

        // The field may have been invalidated in the meantime (for example if
        // the host object has been in-place converted to a different type of
        // object). In that case, handle_location is invalid so we can't
        // evacuate the old entry, but that is also not necessary since it is
        // guaranteed to be dead.
        if (!space->FieldWasInvalidated(handle_location)) {
          entry_was_resolved = TryResolveEvacuationEntryDuringSweeping(
              i, reinterpret_cast<ExternalPointerHandle*>(handle_location),
              start_of_evacuation_area);
        }

        // If the evacuation entry hasn't been resolved (for whatever reason),
        // we must clear it now as we would otherwise have a stale evacuation
        // entry that we'd try to process again during the next GC.
        if (!entry_was_resolved) {
          AddToFreelist(i);
        }
      } else if (!payload.HasMarkBitSet()) {
        AddToFreelist(i);
      } else {
        auto new_payload = payload;
        new_payload.ClearMarkBit();
        ExternalEntityTable<Entry, size>::at(i).SetRawPayload(new_payload);
      }

      // We must have resolved all evacuation entries. Otherwise, we'll try to
      // process them again during the next GC, which would cause problems.
      DCHECK(!this->at(i).HasEvacuationEntry());
    }

    // If a segment is completely empty, free it.
    uint32_t free_entries = current_freelist_length - previous_freelist_length;
    bool segment_is_empty =
        free_entries == ExternalEntityTable<Entry, size>::kEntriesPerSegment;
    if (segment_is_empty) {
      segments_to_deallocate.push_back(segment);
      // Restore the state of the freelist before this segment.
      current_freelist_head = previous_freelist_head;
      current_freelist_length = previous_freelist_length;
    }
  }

  // We cannot deallocate the segments during the above loop, so do it now.
  for (auto segment : segments_to_deallocate) {
    ExternalEntityTable<Entry, size>::FreeTableSegment(segment);
    space->segments_.erase(segment);
  }

  space->ClearInvalidatedFields();

  typename ExternalEntityTable<Entry, size>::FreelistHead new_freelist(
      current_freelist_head, current_freelist_length);
  space->freelist_head_.store(new_freelist, std::memory_order_release);
  DCHECK_EQ(space->freelist_length(), current_freelist_length);

  uint32_t num_live_entries = space->capacity() - current_freelist_length;
  counters->external_pointers_count()->AddSample(num_live_entries);
  return num_live_entries;
}

template <typename Entry, size_t size>
void CompactibleExternalEntityTable<Entry,
                                    size>::Space::StartCompactingIfNeeded() {
  // Take the lock so that we can be sure that no other thread modifies the
  // segments set concurrently.
  base::MutexGuard guard(&this->mutex_);

  // This method may be executed while other threads allocate entries from the
  // freelist. In that case, this method may use incorrect data to determine if
  // table compaction is necessary. That's fine however since in the worst
  // case, compaction will simply be aborted right away if the freelist became
  // too small.
  uint32_t num_free_entries = this->freelist_length();
  uint32_t num_total_entries = this->capacity();

  // Current (somewhat arbitrary) heuristic: need compacting if the space is
  // more than 1MB in size, is at least 10% empty, and if at least one segment
  // can be freed after successful compaction.
  double free_ratio = static_cast<double>(num_free_entries) /
                      static_cast<double>(num_total_entries);
  uint32_t num_segments_to_evacuate =
      (num_free_entries / 2) /
      ExternalEntityTable<Entry, size>::kEntriesPerSegment;

  uint32_t space_size =
      num_total_entries * ExternalEntityTable<Entry, size>::kEntrySize;
  bool should_compact = (space_size >= 1 * MB) && (free_ratio >= 0.10) &&
                        (num_segments_to_evacuate >= 1);

  if (should_compact) {
    // If we're compacting, attempt to free up the last N segments so that they
    // can be decommitted afterwards.
    typename ExternalEntityTable<Entry, size>::Segment
        first_segment_to_evacuate =
            *std::prev(this->segments_.end(), num_segments_to_evacuate);
    uint32_t start_of_evacuation_area = first_segment_to_evacuate.first_entry();
    StartCompacting(start_of_evacuation_area);
  }
}

template <typename Entry, size_t size>
bool CompactibleExternalEntityTable<Entry, size>::
    TryResolveEvacuationEntryDuringSweeping(
        uint32_t new_index, ExternalPointerHandle* handle_location,
        uint32_t start_of_evacuation_area) {
  // We must have a valid handle here. If this fails, it might mean that an
  // object with external pointers was in-place converted to another type of
  // object without informing the external pointer table.
  ExternalPointerHandle old_handle = *handle_location;
  CHECK(IsValidHandle(old_handle));

  uint32_t old_index = HandleToIndex(old_handle);
  ExternalPointerHandle new_handle = IndexToHandle(new_index);

  // It can happen that an external pointer field is cleared (set to the null
  // handle) or even re-initialized between marking and sweeping. In both
  // cases, compacting the entry is not necessary: if it has been cleared, the
  // entry should remain cleared. If it has also been re-initialized, the new
  // table entry must've been allocated at the front of the table, below the
  // evacuation area (otherwise compaction would've been aborted).
  if (old_index < start_of_evacuation_area) {
    return false;
  }

  // The compaction algorithm always moves an entry from the evacuation area to
  // the front of the table. These DCHECKs verify this invariant.
  DCHECK_GE(old_index, start_of_evacuation_area);
  DCHECK_LT(new_index, start_of_evacuation_area);
  auto& new_entry = ExternalEntityTable<Entry, size>::at(new_index);
  ExternalEntityTable<Entry, size>::at(old_index).UnmarkAndMigrateInto(
      new_entry);
  *handle_location = new_handle;
  return true;
}

template <typename Entry, size_t size>
bool CompactibleExternalEntityTable<Entry, size>::IsValidHandle(
    ExternalPointerHandle handle) {
  uint32_t index = handle >> kExternalPointerIndexShift;
  return handle == index << kExternalPointerIndexShift;
}

template <typename Entry, size_t size>
uint32_t CompactibleExternalEntityTable<Entry, size>::HandleToIndex(
    ExternalPointerHandle handle) {
  DCHECK(IsValidHandle(handle));
  uint32_t index = handle >> kExternalPointerIndexShift;
#if defined(LEAK_SANITIZER)
  // When LSan is active, we use "fat" entries that also store the raw pointer
  // to that LSan can find live references. However, we do this transparently:
  // we simply multiply the handle by two so that `(handle >> index_shift) * 8`
  // still produces the correct offset of the entry in the table. However, this
  // is not secure as an attacker could reference the raw pointer instead of
  // the encoded pointer in an entry, thereby bypassing the type checks. As
  // such, this mode must only be used in testing environments. Alternatively,
  // all places that access external pointer table entries must be made aware
  // that the entries are 16 bytes large when LSan is active.
  index /= 2;
#endif  // LEAK_SANITIZER
  DCHECK_LE(index, kMaxExternalPointers);
  return index;
}

template <typename Entry, size_t size>
ExternalPointerHandle
CompactibleExternalEntityTable<Entry, size>::IndexToHandle(uint32_t index) {
  DCHECK_LE(index, kMaxExternalPointers);
  ExternalPointerHandle handle = index << kExternalPointerIndexShift;
#if defined(LEAK_SANITIZER)
  handle *= 2;
#endif  // LEAK_SANITIZER
  DCHECK_NE(handle, kNullExternalPointerHandle);
  return handle;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_COMPACTIBLE_EXTERNAL_ENTITY_TABLE_INL_H_
