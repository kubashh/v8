// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_BUFFER_TABLE_INL_H_
#define V8_SANDBOX_EXTERNAL_BUFFER_TABLE_INL_H_

#include "src/sandbox/compactible-external-entity-table-inl.h"
#include "src/sandbox/external-buffer-table.h"
#include "src/sandbox/external-pointer.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8 {
namespace internal {

void ExternalBufferTableEntry::MakeExternalBufferEntry(
    std::pair<Address, size_t> buffer, ExternalPointerTag tag) {
  DCHECK_EQ(0, buffer.first & kExternalPointerTagMask);
  DCHECK(tag & kExternalPointerMarkBit);
  DCHECK_NE(tag, kExternalPointerFreeEntryTag);
  DCHECK_NE(tag, kExternalPointerEvacuationEntryTag);

  Payload new_payload(buffer, tag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

std::pair<Address, size_t> ExternalBufferTableEntry::GetExternalBuffer(
    ExternalPointerTag tag) const {
  auto payload = payload_.load(std::memory_order_relaxed);
  DCHECK(payload.ContainsExternalPointer());
  return payload.Untag(tag);
}

void ExternalBufferTableEntry::SetExternalBuffer(
    std::pair<Address, size_t> buffer, ExternalPointerTag tag) {
  DCHECK_EQ(0, buffer.first & kExternalPointerTagMask);
  DCHECK(tag & kExternalPointerMarkBit);
  DCHECK(payload_.load(std::memory_order_relaxed).ContainsExternalPointer());

  Payload new_payload(buffer, tag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

bool ExternalBufferTableEntry::HasExternalBuffer(ExternalPointerTag tag) const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return tag == kAnyExternalPointerTag || payload.IsTaggedWith(tag);
}

std::pair<Address, size_t> ExternalBufferTableEntry::ExchangeExternalBuffer(
    std::pair<Address, size_t> buffer, ExternalPointerTag tag) {
  DCHECK_EQ(0, buffer.first & kExternalPointerTagMask);
  DCHECK(tag & kExternalPointerMarkBit);

  Payload new_payload(buffer, tag);
  Payload old_payload =
      payload_.exchange(new_payload, std::memory_order_relaxed);
  DCHECK(old_payload.ContainsExternalPointer());
  return old_payload.Untag(tag);
}

void ExternalBufferTableEntry::MakeFreelistEntry(uint32_t next_entry_index) {
  // The next freelist entry is stored in the lower bits of the entry.
  static_assert(kMaxExternalPointers <= std::numeric_limits<uint32_t>::max());
  Payload new_payload(next_entry_index, kExternalPointerFreeEntryTag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

uint32_t ExternalBufferTableEntry::GetNextFreelistEntryIndex() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.ExtractFreelistLink();
}

void ExternalBufferTableEntry::Mark() {
  auto old_payload = payload_.load(std::memory_order_relaxed);
  DCHECK(old_payload.ContainsExternalPointer());

  auto new_payload = old_payload;
  new_payload.SetMarkBit();

  // We don't need to perform the CAS in a loop: if the new value is not equal
  // to the old value, then the mutator must've just written a new value into
  // the entry. This in turn must've set the marking bit already (see e.g.
  // StoreExternalPointer), so we don't need to do it again.
  bool success = payload_.compare_exchange_strong(old_payload, new_payload,
                                                  std::memory_order_relaxed);
  DCHECK(success || old_payload.HasMarkBitSet());
  USE(success);
}

void ExternalBufferTableEntry::MakeEvacuationEntry(Address handle_location) {
  Payload new_payload(handle_location, kExternalPointerEvacuationEntryTag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

bool ExternalBufferTableEntry::HasEvacuationEntry() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.ContainsEvacuationEntry();
}

void ExternalBufferTableEntry::UnmarkAndMigrateInto(
    ExternalBufferTableEntry& other) {
  auto payload = payload_.load(std::memory_order_relaxed);
  // We expect to only migrate entries containing external pointers.
  DCHECK(payload.ContainsExternalPointer());

  // During compaction, entries that are evacuated may not be visited during
  // sweeping and may therefore still have their marking bit set. As such, we
  // should clear that here.
  payload.ClearMarkBit();

  other.payload_.store(payload, std::memory_order_relaxed);

#ifdef DEBUG
  // In debug builds, we clobber this old entry so that any sharing of table
  // entries is easily detected. Shared entries would require write barriers,
  // so we'd like to avoid them. See the compaction algorithm explanation in
  // external-pointer-table.h for more details.
  constexpr Address kClobberedEntryMarker = static_cast<Address>(-1);
  Payload clobbered(kClobberedEntryMarker, kExternalPointerNullTag);
  DCHECK_NE(payload, clobbered);
  payload_.store(clobbered, std::memory_order_relaxed);
#endif  // DEBUG
}

std::pair<Address, size_t> ExternalBufferTable::Get(
    ExternalBufferHandle handle, ExternalPointerTag tag) const {
  uint32_t index = HandleToIndex(handle);
  DCHECK(index == 0 || at(index).HasExternalBuffer(tag));
  return at(index).GetExternalBuffer(tag);
}

void ExternalBufferTable::Set(ExternalBufferHandle handle,
                              std::pair<Address, size_t> buffer,
                              ExternalPointerTag tag) {
  DCHECK_NE(kNullExternalBufferHandle, handle);
  uint32_t index = HandleToIndex(handle);
  at(index).SetExternalBuffer(buffer, tag);
}

std::pair<Address, size_t> ExternalBufferTable::Exchange(
    ExternalBufferHandle handle, std::pair<Address, size_t> buffer,
    ExternalPointerTag tag) {
  DCHECK_NE(kNullExternalBufferHandle, handle);
  uint32_t index = HandleToIndex(handle);
  return at(index).ExchangeExternalBuffer(buffer, tag);
}

ExternalBufferHandle ExternalBufferTable::AllocateAndInitializeEntry(
    Space* space, std::pair<Address, size_t> initial_buffer,
    ExternalPointerTag tag) {
  DCHECK(space->BelongsTo(this));
  uint32_t index = AllocateEntry(space);
  at(index).MakeExternalBufferEntry(initial_buffer, tag);

  // When we're compacting a space, we're trying to move all entries above a
  // threshold index (the start of the evacuation area) into segments below
  // that threshold. However, if the freelist becomes too short and we start
  // allocating entries inside the area that is supposed to be evacuated, we
  // need to abort compaction. This is not just an optimization but is also
  // required for correctness: during sweeping we might otherwise assume that
  // all entries inside the evacuation area have been moved and that these
  // segments can therefore be deallocated. In particular, this check will also
  // make sure that we abort compaction if we extend the space with a new
  // segment and allocate at least one entry in it (if that segment is located
  // after the threshold, otherwise it is unproblematic).
  uint32_t start_of_evacuation_area =
      space->start_of_evacuation_area_.load(std::memory_order_relaxed);
  if (V8_UNLIKELY(index >= start_of_evacuation_area)) {
    space->AbortCompacting(start_of_evacuation_area);
  }

  return IndexToHandle(index);
}

void ExternalBufferTable::Mark(Space* space, ExternalBufferHandle handle,
                               Address handle_location) {
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
  ExternalBufferHandle current_handle = base::AsAtomic32::Acquire_Load(
      reinterpret_cast<ExternalBufferHandle*>(handle_location));
  DCHECK(handle == kNullExternalBufferHandle ||
         current_handle == kNullExternalBufferHandle ||
         handle == current_handle);
#endif

  // The null entry is immortal and immutable, so no need to mark it as alive.
  if (handle == kNullExternalBufferHandle) return;

  uint32_t index = HandleToIndex(handle);
  DCHECK(space->Contains(index));

  // If the table is being compacted and the entry is inside the evacuation
  // area, then allocate and set up an evacuation entry for it.
  MaybeCreateEvacuationEntry(space, index, handle_location);

  // Even if the entry is marked for evacuation, it still needs to be marked as
  // alive as it may be visited during sweeping before being evacuation.
  at(index).Mark();
}

// static
bool ExternalBufferTable::IsValidHandle(ExternalBufferHandle handle) {
  uint32_t index = handle >> kExternalBufferHandleShift;
  return handle == index << kExternalBufferHandleShift;
}

// static
uint32_t ExternalBufferTable::HandleToIndex(ExternalBufferHandle handle) {
  DCHECK(IsValidHandle(handle));
  uint32_t index = handle >> kExternalBufferHandleShift;
  DCHECK_LE(index, kMaxExternalBufferPointers);
  return index;
}

// static
ExternalBufferHandle ExternalBufferTable::IndexToHandle(uint32_t index) {
  DCHECK_LE(index, kMaxExternalBufferPointers);
  ExternalBufferHandle handle = index << kExternalBufferHandleShift;
  DCHECK_NE(handle, kNullExternalBufferHandle);
  return handle;
}

void ExternalBufferTable::Space::NotifyExternalPointerFieldInvalidated(
    Address field_address) {
#ifdef DEBUG
  ExternalBufferHandle handle = base::AsAtomic32::Acquire_Load(
      reinterpret_cast<ExternalBufferHandle*>(field_address));
  DCHECK(Contains(HandleToIndex(handle)));
#endif
  AddInvalidatedField(field_address);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX

#endif  // V8_SANDBOX_EXTERNAL_BUFFER_TABLE_INL_H_
