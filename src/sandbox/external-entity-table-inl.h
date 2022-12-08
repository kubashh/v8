// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_ENTITY_TABLE_INL_H_
#define V8_SANDBOX_EXTERNAL_ENTITY_TABLE_INL_H_

#include "src/base/atomicops.h"
#include "src/common/assert-scope.h"
#include "src/execution/isolate.h"
#include "src/sandbox/external-pointer-table.h"
#include "src/sandbox/external-pointer.h"
#include "src/utils/allocation.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

template <Address kMarkingBit>
Address ExternalEntityTable<kMarkingBit>::Get(ExternalPointerHandle handle,
                                              ExternalPointerTag tag) const {
  uint32_t index = HandleToIndex(handle);
  Entry entry = RelaxedLoad(index);
  DCHECK(entry.IsRegularEntry());
  return entry.Untag(tag);
}

template <Address kMarkingBit>
void ExternalEntityTable<kMarkingBit>::Set(ExternalPointerHandle handle,
                                           Address value,
                                           ExternalPointerTag tag) {
  DCHECK_NE(kNullExternalPointerHandle, handle);
  DCHECK_EQ(0, value & kExternalPointerTagMask);
  // DCHECK(tag & kMarkingBit);

  uint32_t index = HandleToIndex(handle);
  Entry entry = Entry::MakeRegularEntry(value, tag);
  RelaxedStore(index, entry);
}

template <Address kMarkingBit>
Address ExternalEntityTable<kMarkingBit>::Exchange(ExternalPointerHandle handle,
                                                   Address value,
                                                   ExternalPointerTag tag) {
  DCHECK_NE(kNullExternalPointerHandle, handle);
  DCHECK_EQ(0, value & kExternalPointerTagMask);
  // DCHECK(tag & kMarkingBit);

  uint32_t index = HandleToIndex(handle);
  Entry new_entry = Entry::MakeRegularEntry(value, tag);
  Entry old_entry = RelaxedExchange(index, new_entry);
  DCHECK(old_entry.IsRegularEntry());
  return old_entry.Untag(tag);
}

template <Address kMarkingBit>
bool ExternalEntityTable<kMarkingBit>::TryAllocateEntryFromFreelist(
    Freelist freelist) {
  DCHECK(!freelist.IsEmpty());
  DCHECK_LT(freelist.Head(), capacity());
  DCHECK_LT(freelist.Size(), capacity());

  Entry entry = RelaxedLoad(freelist.Head());
  uint32_t new_freelist_head = entry.ExtractNextFreelistEntry();

  Freelist new_freelist(new_freelist_head, freelist.Size() - 1);
  bool success = Relaxed_CompareAndSwapFreelist(freelist, new_freelist);

  // When the CAS succeeded, the entry must've been a freelist entry.
  // Otherwise, this is not guaranteed as another thread may have allocated
  // the same entry in the meantime.
  if (success) {
    DCHECK(entry.IsFreelistEntry());
    DCHECK_LT(new_freelist.Head(), capacity());
    DCHECK_LT(new_freelist.Size(), capacity());
    DCHECK_IMPLIES(freelist.Size() > 1, !new_freelist.IsEmpty());
    DCHECK_IMPLIES(freelist.Size() == 1, new_freelist.IsEmpty());
  }
  return success;
}

template <Address kMarkingBit>
ExternalPointerHandle
ExternalEntityTable<kMarkingBit>::AllocateAndInitializeEntry(
    Isolate* isolate, Address initial_value, ExternalPointerTag tag) {
  DCHECK(is_initialized());

  // We currently don't want entry allocation to trigger garbage collection as
  // this may cause seemingly harmless pointer field assignments to trigger
  // garbage collection. This is especially true for lazily-initialized
  // external pointer slots which will typically only allocate the external
  // pointer table entry when the pointer is first set to a non-null value.
  DisallowGarbageCollection no_gc;

  Freelist freelist;
  bool success = false;
  while (!success) {
    // This is essentially DCLP (see
    // https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11/)
    // and so requires an acquire load as well as a release store in Grow() to
    // prevent reordering of memory accesses, which could for example cause one
    // thread to read a freelist entry before it has been properly initialized.
    freelist = Acquire_GetFreelist();
    if (freelist.IsEmpty()) {
      // Freelist is empty. Need to take the lock, then attempt to grow the
      // table if no other thread has done it in the meantime.
      base::MutexGuard guard(mutex_);

      // Reload freelist head in case another thread already grew the table.
      freelist = Relaxed_GetFreelist();

      if (freelist.IsEmpty()) {
        // Freelist is (still) empty so grow the table.
        freelist = Grow(isolate);
        // Grow() adds one block to the table and so to the freelist.
        DCHECK_EQ(freelist.Size(), kEntriesPerBlock);
      }
    }

    success = TryAllocateEntryFromFreelist(freelist);
  }

  DCHECK_NE(freelist.Head(), 0);
  DCHECK_LT(freelist.Head(), capacity());

  uint32_t entry_index = freelist.Head();
  Entry entry = Entry::MakeRegularEntry(initial_value, tag);
  RelaxedStore(entry_index, entry);

  return IndexToHandle(entry_index);
}

template <Address kMarkingBit>
ExternalPointerHandle ExternalEntityTable<kMarkingBit>::AllocateEvacuationEntry(
    uint32_t start_of_evacuation_area) {
  DCHECK(is_initialized());
  DCHECK_LT(start_of_evacuation_area, capacity());

  Freelist freelist;
  bool success = false;
  while (!success) {
    freelist = Acquire_GetFreelist();
    // Check that the next free entry is below the start of the evacuation area.
    if (freelist.IsEmpty() || freelist.Head() >= start_of_evacuation_area)
      return kNullExternalPointerHandle;

    success = TryAllocateEntryFromFreelist(freelist);
  }

  DCHECK_NE(freelist.Head(), 0);
  DCHECK_LT(freelist.Head(), start_of_evacuation_area);

  return IndexToHandle(freelist.Head());
}

template <Address kMarkingBit>
uint32_t ExternalEntityTable<kMarkingBit>::FreelistSize() {
  Freelist freelist = Relaxed_GetFreelist();
  DCHECK_LE(freelist.Size(), capacity());
  return freelist.Size();
}

template <Address kMarkingBit>
void ExternalEntityTable<kMarkingBit>::Mark(ExternalPointerHandle handle,
                                            Address handle_location) {
  static_assert(sizeof(base::Atomic64) == sizeof(Address));
  // The handle_location must contain the given handle. The only exception to
  // this is when the handle is zero, which means that it hasn't yet been
  // initialized. In that case, the handle may be initialized between the
  // caller loading it and this DCHECK loading it again, in which case the two
  // values would not be the same. This scenario is unproblematic though as the
  // new entry will already be marked as alive as it has just been allocated.
  DCHECK(handle == kNullExternalPointerHandle ||
         handle == *reinterpret_cast<ExternalPointerHandle*>(handle_location));

  uint32_t index = HandleToIndex(handle);

  // Check if the entry should be evacuated for table compaction.
  // The current value of the start of the evacuation area is cached in a local
  // variable here as it otherwise may be changed by another marking thread
  // while this method runs, causing non-optimal behaviour (for example, the
  // allocation of an evacuation entry _after_ the entry that is evacuated).
  uint32_t current_start_of_evacuation_area = start_of_evacuation_area();
  if (index >= current_start_of_evacuation_area) {
    DCHECK(IsCompacting());
    ExternalPointerHandle new_handle =
        AllocateEvacuationEntry(current_start_of_evacuation_area);
    if (new_handle) {
      DCHECK_LT(HandleToIndex(new_handle), current_start_of_evacuation_area);
      uint32_t index = HandleToIndex(new_handle);
      // No need for an atomic store as the entry will only be accessed during
      // sweeping.
      Store(index, Entry::MakeEvacuationEntry(handle_location));
#ifdef DEBUG
      // Mark the handle as visited in debug builds to detect double
      // initialization of external pointer fields.
      auto handle_ptr = reinterpret_cast<base::Atomic32*>(handle_location);
      base::Relaxed_Store(handle_ptr, handle | kVisitedHandleMarker);
#endif  // DEBUG
    } else {
      // In this case, the application has allocated a sufficiently large
      // number of entries from the freelist so that new entries would now be
      // allocated inside the area that is being compacted. While it would be
      // possible to shrink that area and continue compacting, we probably do
      // not want to put more pressure on the freelist and so instead simply
      // abort compaction here. Entries that have already been visited will
      // still be compacted during Sweep, but there is no guarantee that any
      // blocks at the end of the table will now be completely free.
      uint32_t compaction_aborted_marker =
          current_start_of_evacuation_area | kCompactionAbortedMarker;
      set_start_of_evacuation_area(compaction_aborted_marker);
    }
  }
  // Even if the entry is marked for evacuation, it still needs to be marked as
  // alive as it may be visited during sweeping before being evacuation.

  Entry old_entry = RelaxedLoad(index);
  DCHECK(old_entry.IsRegularEntry());

  Entry new_entry = old_entry;
  new_entry.SetMarkBit();

  // We don't need to perform the CAS in a loop: if the new value is not equal
  // to the old value, then the mutator must've just written a new value into
  // the entry. This in turn must've set the marking bit already (see
  // ExternalEntityTable::Set), so we don't need to do it again.
  Entry entry = RelaxedCompareAndSwap(index, old_entry, new_entry);
  DCHECK((entry == old_entry) || entry.IsMarked());
  USE(entry);
}

template <Address kMarkingBit>
bool ExternalEntityTable<kMarkingBit>::IsCompacting() {
  return start_of_evacuation_area() != kNotCompactingMarker;
}

template <Address kMarkingBit>
bool ExternalEntityTable<kMarkingBit>::CompactingWasAbortedDuringMarking() {
  return (start_of_evacuation_area() & kCompactionAbortedMarker) ==
         kCompactionAbortedMarker;
}

template <Address kMarkingBit>
void ExternalEntityTable<kMarkingBit>::Init(Isolate* isolate) {
  DCHECK(!is_initialized());

  VirtualAddressSpace* root_space = GetPlatformVirtualAddressSpace();
  DCHECK(IsAligned(kExternalPointerTableReservationSize,
                   root_space->allocation_granularity()));

  size_t reservation_size = kExternalPointerTableReservationSize;
#if defined(LEAK_SANITIZER)
  // When LSan is active, we use a "shadow table" which contains the raw
  // pointers stored in this external pointer table so that LSan can scan them.
  // This is necessary to avoid false leak reports. The shadow table is located
  // right after the real table in memory. See also lsan_record_ptr().
  reservation_size *= 2;
#endif  // LEAK_SANITIZER

  buffer_ = root_space->AllocatePages(
      VirtualAddressSpace::kNoHint, reservation_size,
      root_space->allocation_granularity(), PagePermissions::kNoAccess);
  if (!buffer_) {
    V8::FatalProcessOutOfMemory(
        isolate,
        "Failed to reserve memory for ExternalEntityTable backing buffer");
  }

  mutex_ = new base::Mutex;
  if (!mutex_) {
    V8::FatalProcessOutOfMemory(
        isolate, "Failed to allocate mutex for ExternalEntityTable");
  }

  // Allocate the initial block. Mutex must be held for that.
  base::MutexGuard guard(mutex_);
  Grow(isolate);

  // Set up the special null entry. This entry must contain nullptr so that
  // empty EmbedderDataSlots represent nullptr.
  static_assert(kNullExternalPointerHandle == 0);
  Store(0, Entry::MakeNullEntry());
}

template <Address kMarkingBit>
void ExternalEntityTable<kMarkingBit>::TearDown() {
  DCHECK(is_initialized());

  size_t reservation_size = kExternalPointerTableReservationSize;
#if defined(LEAK_SANITIZER)
  reservation_size *= 2;
#endif  // LEAK_SANITIZER

  GetPlatformVirtualAddressSpace()->FreePages(buffer_, reservation_size);
  delete mutex_;

  buffer_ = kNullAddress;
  capacity_ = 0;
  freelist_ = 0;
  mutex_ = nullptr;
}

template <Address kMarkingBit>
uint32_t ExternalEntityTable<kMarkingBit>::SweepAndCompact(Isolate* isolate) {
  // There must not be any entry allocations while the table is being swept as
  // that would not be safe. Set the freelist to this special marker value to
  // better catch any violation of this requirement.
  Freelist old_freelist = Relaxed_GetFreelist();
  base::Release_Store(&freelist_, kTableIsCurrentlySweepingMarker);

  // Keep track of the last block (identified by the index of its first entry)
  // that has live entries. Used to decommit empty blocks at the end.
  DCHECK_GE(capacity(), kEntriesPerBlock);
  const uint32_t last_block = capacity() - kEntriesPerBlock;
  uint32_t last_in_use_block = last_block;

  // When compacting, we can compute the number of unused blocks at the end of
  // the table and skip those during sweeping.
  uint32_t first_block_of_evacuation_area = start_of_evacuation_area();
  if (IsCompacting()) {
    TableCompactionOutcome outcome;
    if (CompactingWasAbortedDuringMarking()) {
      // Compaction was aborted during marking because the freelist grew to
      // short. This is not great because now there is no guarantee that any
      // blocks will be emtpy and so the entire table needs to be swept.
      outcome = TableCompactionOutcome::kAbortedDuringMarking;
      // Extract the original start_of_evacuation_area value so that the
      // DCHECKs below work correctly.
      first_block_of_evacuation_area &= ~kCompactionAbortedMarker;
    } else if (old_freelist.IsEmpty() ||
               old_freelist.Head() > first_block_of_evacuation_area) {
      // In this case, marking finished successfully, but the application
      // afterwards allocated entries inside the area that is being compacted.
      // In this case, we can still compute how many blocks at the end of the
      // table are now empty.
      if (!old_freelist.IsEmpty()) {
        last_in_use_block = RoundDown(old_freelist.Head(), kEntriesPerBlock);
      }
      outcome = TableCompactionOutcome::kPartialSuccess;
    } else {
      // Marking was successful so the entire evacuation area is now free.
      last_in_use_block = first_block_of_evacuation_area - kEntriesPerBlock;
      outcome = TableCompactionOutcome::kSuccess;
    }
    isolate->counters()->external_pointer_table_compaction_outcome()->AddSample(
        static_cast<int>(outcome));
    DCHECK(IsAligned(first_block_of_evacuation_area, kEntriesPerBlock));
  }

  // Sweep top to bottom and rebuild the freelist from newly dead and
  // previously freed entries while also clearing the marking bit on live
  // entries and resolving evacuation entries table when compacting the table.
  // This way, the freelist ends up sorted by index which already makes the
  // table somewhat self-compacting and is required for the compaction
  // algorithm so that evacuated entries are evacuated to the start of the
  // table. This method must run either on the mutator thread or while the
  // mutator is stopped.
  uint32_t current_freelist_size = 0;
  uint32_t current_freelist_head = 0;

  // Skip the special null entry. This also guarantees that the first block
  // will never be decommitted.
  // The null entry may have been marked as alive (if any live object was
  // referencing it), which is fine, the entry will just keep the bit set.
  DCHECK_GE(capacity(), 1);
  uint32_t table_end = last_in_use_block + kEntriesPerBlock;
  DCHECK(IsAligned(table_end, kEntriesPerBlock));
  for (uint32_t i = table_end - 1; i > 0; i--) {
    // No other threads are active during sweep, so there is no need to use
    // atomic operations here.
    Entry entry = Load(i);
    if (entry.IsEvacuationEntry()) {
      // Resolve the evacuation entry: take the pointer to the handle from the
      // evacuation entry, copy the entry to its new location, and finally
      // update the handle to point to the new entry.
      ExternalPointerHandle* handle_location =
          reinterpret_cast<ExternalPointerHandle*>(
              entry.ExtractHandleLocation());

      ExternalPointerHandle old_handle = *handle_location;
      ExternalPointerHandle new_handle = IndexToHandle(i);

      // For the compaction algorithm to work optimally, double initialization
      // of entries is forbidden, see below. This DCHECK can detect double
      // initialization of external pointer fields in debug builds by checking
      // that the old_handle was visited during marking.
      // There's no need to clear the marking bit from the handle as the handle
      // will be replaced by a new, unmarked handle.
      DCHECK(HandleWasVisitedDuringMarking(old_handle));

      // The following DCHECKs assert that the compaction algorithm works
      // correctly: it always moves an entry from the evacuation area to the
      // front of the table. One reason this invariant can be broken is if an
      // external pointer slot is re-initialized, in which case the old_handle
      // may now also point before the evacuation area. For that reason,
      // re-initialization of external pointer slots is forbidden.
      DCHECK_GE(HandleToIndex(old_handle), first_block_of_evacuation_area);
      DCHECK_LT(HandleToIndex(new_handle), first_block_of_evacuation_area);

      Entry entry_to_evacuate = Load(HandleToIndex(old_handle));
      entry_to_evacuate.ClearMarkBit();
      Store(i, entry_to_evacuate);
      *handle_location = new_handle;

#ifdef DEBUG
      // In debug builds, clobber the old entry so that any sharing of table
      // entries is easily detected. Shared entries would require write
      // barriers, so we'd like to avoid them. See the compaction algorithm
      // explanation in external-pointer-table.h for more details.
      constexpr Address kClobberedEntryMarker = static_cast<Address>(-1);
      const Entry kClobberedEntry = Entry::Decode(kClobberedEntryMarker);
      DCHECK_NE(entry_to_evacuate, kClobberedEntry);
      Store(HandleToIndex(old_handle), kClobberedEntry);
#endif  //  DEBUG

      // While we know that the old entry is now free, we don't add it to (the
      // start of) the freelist because that would immediately cause new
      // fragmentation when the next entry is allocated. Instead, we assume
      // that the blocks out of which entries are evacuated will all be
      // decommitted anyway after this loop, which is usually the case unless
      // compaction was already aborted during marking.
    } else if (!entry.IsMarked()) {
      current_freelist_size++;
      Entry entry = Entry::MakeFreelistEntry(current_freelist_head);
      Store(i, entry);
      current_freelist_head = i;
    } else {
      entry.ClearMarkBit();
      Store(i, entry);
    }

    if (last_in_use_block == i) {
      // Finished iterating over the last in-use block. Now see if it is
      // empty.
      if (current_freelist_size == kEntriesPerBlock) {
        // Block is completely empty, so mark it for decommitting.
        last_in_use_block -= kEntriesPerBlock;
        // Freelist is now empty again.
        current_freelist_head = 0;
        current_freelist_size = 0;
      }
    }
  }

  // Decommit all blocks at the end of the table that are not used anymore.
  if (last_in_use_block != last_block) {
    uint32_t new_capacity = last_in_use_block + kEntriesPerBlock;
    DCHECK_LT(new_capacity, capacity());
    Address new_table_end = buffer_ + new_capacity * sizeof(Address);
    uint32_t bytes_to_decommit = (capacity() - new_capacity) * sizeof(Address);
    set_capacity(new_capacity);

    VirtualAddressSpace* root_space = GetPlatformVirtualAddressSpace();
    // The pages may contain stale pointers which could be abused by an
    // attacker if they are still accessible, so use Decommit here which
    // guarantees that the pages become inaccessible and will be zeroed out.
    CHECK(root_space->DecommitPages(new_table_end, bytes_to_decommit));

#if defined(LEAK_SANITIZER)
    Address new_shadow_table_end = buffer_ +
                                   kExternalPointerTableReservationSize +
                                   new_capacity * sizeof(Address);
    CHECK(root_space->DecommitPages(new_shadow_table_end, bytes_to_decommit));
#endif  // LEAK_SANITIZER
  }

  if (IsCompacting()) {
    StopCompacting();
  }

  Freelist new_freelist(current_freelist_head, current_freelist_size);
  Release_SetFreelist(new_freelist);

  uint32_t num_active_entries = capacity() - current_freelist_size;
  isolate->counters()->external_pointers_count()->AddSample(num_active_entries);
  return num_active_entries;
}

template <Address kMarkingBit>
void ExternalEntityTable<kMarkingBit>::StartCompactingIfNeeded() {
  // This method may be executed while other threads allocate entries from the
  // freelist or even grow the table, thereby increasing the capacity. In that
  // case, this method may use incorrect data to determine if table compaction
  // is necessary. That's fine however since in the worst case, compaction will
  // simply be aborted right away if the freelist became too small.
  uint32_t freelist_size = FreelistSize();
  uint32_t current_capacity = capacity();

  // Current (somewhat arbitrary) heuristic: need compacting if the table is
  // more than 1MB in size, is at least 10% empty, and if at least one block
  // can be decommitted after successful compaction.
  uint32_t table_size = current_capacity * kSystemPointerSize;
  double free_ratio = static_cast<double>(freelist_size) /
                      static_cast<double>(current_capacity);
  uint32_t num_blocks_to_evacuate = (freelist_size / 2) / kEntriesPerBlock;
  bool should_compact = (table_size >= 1 * MB) && (free_ratio >= 0.10) &&
                        (num_blocks_to_evacuate >= 1);

  if (should_compact) {
    uint32_t num_entries_to_evacuate =
        num_blocks_to_evacuate * kEntriesPerBlock;
    set_start_of_evacuation_area(current_capacity - num_entries_to_evacuate);
  }
}

template <Address kMarkingBit>
void ExternalEntityTable<kMarkingBit>::StopCompacting() {
  DCHECK(IsCompacting());
  set_start_of_evacuation_area(kNotCompactingMarker);
}

template <Address kMarkingBit>
typename ExternalEntityTable<kMarkingBit>::Freelist
ExternalEntityTable<kMarkingBit>::Grow(Isolate* isolate) {
  // Freelist should be empty when calling this method.
  DCHECK(Relaxed_GetFreelist().IsEmpty());
  // Mutex must be held when calling this method.
  mutex_->AssertHeld();

  // Grow the table by one block.
  VirtualAddressSpace* root_space = GetPlatformVirtualAddressSpace();
  DCHECK(IsAligned(kBlockSize, root_space->page_size()));
  uint32_t old_capacity = capacity();
  uint32_t new_capacity = old_capacity + kEntriesPerBlock;
  if (new_capacity > kMaxExternalPointers) {
    V8::FatalProcessOutOfMemory(
        isolate, "Cannot grow ExternalEntityTable past its maximum capacity");
  }
  if (!root_space->SetPagePermissions(buffer_ + old_capacity * sizeof(Address),
                                      kBlockSize,
                                      PagePermissions::kReadWrite)) {
    V8::FatalProcessOutOfMemory(
        isolate, "Failed to grow the ExternalEntityTable backing buffer");
  }

#if defined(LEAK_SANITIZER)
  if (!root_space->SetPagePermissions(
          buffer_ + kExternalPointerTableReservationSize +
              old_capacity * sizeof(Address),
          kBlockSize, PagePermissions::kReadWrite)) {
    V8::FatalProcessOutOfMemory(
        isolate, "Failed to grow the ExternalPointerTabl shadow table");
  }
#endif  // LEAK_SANITIZER

  set_capacity(new_capacity);

  // Build freelist bottom to top, which might be more cache friendly.
  uint32_t start = std::max<uint32_t>(old_capacity, 1);  // Skip entry zero
  uint32_t last = new_capacity - 1;
  for (uint32_t i = start; i < last; i++) {
    uint32_t next_free_entry = i + 1;
    Store(i, Entry::MakeFreelistEntry(next_free_entry));
  }
  Store(last, Entry::MakeFreelistEntry(0));

  // This must be a release store to prevent reordering of the preceeding
  // stores to the freelist from being reordered past this store. See
  // AllocateAndInitializeEntry() for more details.
  Freelist new_freelist(start, last - start + 1);
  Release_SetFreelist(new_freelist);

  return new_freelist;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_EXTERNAL_ENTITY_TABLE_INL_H_
