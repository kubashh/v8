// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_
#define V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_

#include "src/sandbox/compactible-external-entity-table-inl.h"
#include "src/sandbox/external-pointer-table.h"
#include "src/sandbox/external-pointer.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

void ExternalPointerTableEntry::MakeExternalPointerEntry(
    Address value, ExternalPointerTag tag) {
  DCHECK_EQ(0, value & kExternalPointerTagMask);
  DCHECK(tag & kExternalPointerMarkBit);
  DCHECK_NE(tag, kExternalPointerFreeEntryTag);
  DCHECK_NE(tag, kExternalPointerEvacuationEntryTag);

  Payload new_payload(value, tag);
  payload_.store(new_payload, std::memory_order_relaxed);
  MaybeUpdateRawPointerForLSan(value);
}

Address ExternalPointerTableEntry::GetExternalPointer(
    ExternalPointerTag tag) const {
  auto payload = payload_.load(std::memory_order_relaxed);
  DCHECK(payload.ContainsExternalPointer());
  return payload.Untag(tag);
}

void ExternalPointerTableEntry::SetExternalPointer(Address value,
                                                   ExternalPointerTag tag) {
  DCHECK_EQ(0, value & kExternalPointerTagMask);
  DCHECK(tag & kExternalPointerMarkBit);
  DCHECK(payload_.load(std::memory_order_relaxed).ContainsExternalPointer());

  Payload new_payload(value, tag);
  payload_.store(new_payload, std::memory_order_relaxed);
  MaybeUpdateRawPointerForLSan(value);
}

bool ExternalPointerTableEntry::HasExternalPointer(
    ExternalPointerTag tag) const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return tag == kAnyExternalPointerTag || payload.IsTaggedWith(tag);
}

Address ExternalPointerTableEntry::ExchangeExternalPointer(
    Address value, ExternalPointerTag tag) {
  DCHECK_EQ(0, value & kExternalPointerTagMask);
  DCHECK(tag & kExternalPointerMarkBit);

  Payload new_payload(value, tag);
  Payload old_payload =
      payload_.exchange(new_payload, std::memory_order_relaxed);
  DCHECK(old_payload.ContainsExternalPointer());
  MaybeUpdateRawPointerForLSan(value);
  return old_payload.Untag(tag);
}

void ExternalPointerTableEntry::MakeFreelistEntry(uint32_t next_entry_index) {
  // The next freelist entry is stored in the lower bits of the entry.
  static_assert(kMaxExternalPointers <= std::numeric_limits<uint32_t>::max());
  Payload new_payload(next_entry_index, kExternalPointerFreeEntryTag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

uint32_t ExternalPointerTableEntry::GetNextFreelistEntryIndex() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.ExtractFreelistLink();
}

void ExternalPointerTableEntry::Mark() {
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

void ExternalPointerTableEntry::MakeEvacuationEntry(Address handle_location) {
  Payload new_payload(handle_location, kExternalPointerEvacuationEntryTag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

bool ExternalPointerTableEntry::HasEvacuationEntry() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.ContainsEvacuationEntry();
}

void ExternalPointerTableEntry::UnmarkAndMigrateInto(
    ExternalPointerTableEntry& other) {
  auto payload = payload_.load(std::memory_order_relaxed);
  // We expect to only migrate entries containing external pointers.
  DCHECK(payload.ContainsExternalPointer());

  // During compaction, entries that are evacuated may not be visited during
  // sweeping and may therefore still have their marking bit set. As such, we
  // should clear that here.
  payload.ClearMarkBit();

  other.payload_.store(payload, std::memory_order_relaxed);
#if defined(LEAK_SANITIZER)
  other.raw_pointer_for_lsan_ = raw_pointer_for_lsan_;
#endif  // LEAK_SANITIZER

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

Address ExternalPointerTable::Get(ExternalPointerHandle handle,
                                  ExternalPointerTag tag) const {
  uint32_t index = HandleToIndex(handle);
#if defined(V8_USE_ADDRESS_SANITIZER)
  // We rely on the tagging scheme to produce non-canonical addresses when an
  // entry isn't tagged with the expected tag. Such "safe" crashes can then be
  // filtered out by our sandbox crash filter. However, when ASan is active, it
  // may perform its shadow memory access prior to the actual memory access.
  // For a non-canonical address, this can lead to a segfault at a _canonical_
  // address, which our crash filter can then not distinguish from a "real"
  // crash. Therefore, in ASan builds, we perform an additional CHECK here that
  // the entry is tagged with the expected tag. The resulting CHECK failure
  // will then be ignored by the crash filter.
  // This check is, however, not needed when accessing the null entry, as that
  // is always valid (it just contains nullptr).
  CHECK(index == 0 || at(index).HasExternalPointer(tag));
#else
  // Otherwise, this is just a DCHECK.
  DCHECK(index == 0 || at(index).HasExternalPointer(tag));
#endif
  return at(index).GetExternalPointer(tag);
}

void ExternalPointerTable::Set(ExternalPointerHandle handle, Address value,
                               ExternalPointerTag tag) {
  DCHECK_NE(kNullExternalPointerHandle, handle);
  uint32_t index = HandleToIndex(handle);
  at(index).SetExternalPointer(value, tag);
}

Address ExternalPointerTable::Exchange(ExternalPointerHandle handle,
                                       Address value, ExternalPointerTag tag) {
  DCHECK_NE(kNullExternalPointerHandle, handle);
  uint32_t index = HandleToIndex(handle);
  return at(index).ExchangeExternalPointer(value, tag);
}

ExternalPointerHandle ExternalPointerTable::AllocateAndInitializeEntry(
    Space* space, Address initial_value, ExternalPointerTag tag) {
  DCHECK(space->BelongsTo(this));
  uint32_t index = AllocateEntry(space);
  at(index).MakeExternalPointerEntry(initial_value, tag);

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

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_
