// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/paged-spaces.h"

#include "src/base/optional.h"
#include "src/base/platform/mutex.h"
#include "src/execution/isolate.h"
#include "src/execution/vm-state-inl.h"
#include "src/heap/array-buffer-sweeper.h"
#include "src/heap/heap.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/memory-allocator.h"
#include "src/heap/memory-chunk-inl.h"
#include "src/heap/paged-spaces-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/logging/counters.h"
#include "src/objects/string.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

// ----------------------------------------------------------------------------
// PagedSpaceObjectIterator

PagedSpaceObjectIterator::PagedSpaceObjectIterator(Heap* heap,
                                                   PagedSpace* space)
    : cur_addr_(kNullAddress),
      cur_end_(kNullAddress),
      space_(space),
      page_range_(space->first_page(), nullptr),
      current_page_(page_range_.begin()) {
  heap->mark_compact_collector()->EnsureSweepingCompleted();
}

PagedSpaceObjectIterator::PagedSpaceObjectIterator(Heap* heap,
                                                   PagedSpace* space,
                                                   Page* page)
    : cur_addr_(kNullAddress),
      cur_end_(kNullAddress),
      space_(space),
      page_range_(page),
      current_page_(page_range_.begin()) {
  heap->mark_compact_collector()->EnsureSweepingCompleted();
#ifdef DEBUG
  AllocationSpace owner = page->owner_identity();
  DCHECK(owner == OLD_SPACE || owner == MAP_SPACE || owner == CODE_SPACE);
#endif  // DEBUG
}

// We have hit the end of the page and should advance to the next block of
// objects.  This happens at the end of the page.
bool PagedSpaceObjectIterator::AdvanceToNextPage() {
  DCHECK_EQ(cur_addr_, cur_end_);
  if (current_page_ == page_range_.end()) return false;
  Page* cur_page = *(current_page_++);

  cur_addr_ = cur_page->area_start();
  cur_end_ = cur_page->area_end();
  DCHECK(cur_page->SweepingDone());
  return true;
}
Page* PagedSpace::InitializePage(MemoryChunk* chunk) {
  Page* page = static_cast<Page*>(chunk);
  DCHECK_EQ(
      MemoryChunkLayout::AllocatableMemoryInMemoryChunk(page->owner_identity()),
      page->area_size());
  // Make sure that categories are initialized before freeing the area.
  page->ResetAllocationStatistics();
  page->SetOldGenerationPageFlags(heap()->incremental_marking()->IsMarking());
  page->AllocateFreeListCategories();
  page->InitializeFreeListCategories();
  page->list_node().Initialize();
  page->InitializationMemoryFence();
  return page;
}

PagedSpace::PagedSpace(Heap* heap, AllocationSpace space,
                       Executability executable, FreeList* free_list,
                       LocalSpaceKind local_space_kind)
    : Space(heap, space, free_list),
      executable_(executable),
      local_space_kind_(local_space_kind) {
  area_size_ = MemoryChunkLayout::AllocatableMemoryInMemoryChunk(space);
  accounting_stats_.Clear();
}

void PagedSpace::TearDown() {
  while (!memory_chunk_list_.Empty()) {
    MemoryChunk* chunk = memory_chunk_list_.front();
    memory_chunk_list_.Remove(chunk);
    heap()->memory_allocator()->Free<MemoryAllocator::kFull>(chunk);
  }
  accounting_stats_.Clear();
}

void PagedSpace::RefillFreeList() {
  // Any PagedSpace might invoke RefillFreeList. We filter all but our old
  // generation spaces out.
  if (identity() != OLD_SPACE && identity() != CODE_SPACE &&
      identity() != MAP_SPACE) {
    return;
  }
  DCHECK_IMPLIES(is_local_space(), is_compaction_space());
  MarkCompactCollector* collector = heap()->mark_compact_collector();
  size_t added = 0;

  {
    Page* p = nullptr;
    while ((p = collector->sweeper()->GetSweptPageSafe(this)) != nullptr) {
      // We regularly sweep NEVER_ALLOCATE_ON_PAGE pages. We drop the freelist
      // entries here to make them unavailable for allocations.
      if (p->IsFlagSet(Page::NEVER_ALLOCATE_ON_PAGE)) {
        p->ForAllFreeListCategories([this](FreeListCategory* category) {
          category->Reset(free_list());
        });
      }

      // Also merge old-to-new remembered sets if not scavenging because of
      // data races: One thread might iterate remembered set, while another
      // thread merges them.
      if (local_space_kind() != LocalSpaceKind::kCompactionSpaceForScavenge) {
        p->MergeOldToNewRememberedSets();
      }

      // Only during compaction pages can actually change ownership. This is
      // safe because there exists no other competing action on the page links
      // during compaction.
      if (is_compaction_space()) {
        DCHECK_NE(this, p->owner());
        PagedSpace* owner = reinterpret_cast<PagedSpace*>(p->owner());
        base::MutexGuard guard(owner->mutex());
        owner->RefineAllocatedBytesAfterSweeping(p);
        owner->RemovePage(p);
        added += AddPage(p);
        added += p->wasted_memory();
      } else {
        base::MutexGuard guard(mutex());
        DCHECK_EQ(this, p->owner());
        RefineAllocatedBytesAfterSweeping(p);
        added += RelinkFreeListCategories(p);
        added += p->wasted_memory();
      }
      if (is_compaction_space() && (added > kCompactionMemoryWanted)) break;
    }
  }
}

void PagedSpace::MergeLocalSpace(LocalSpace* other) {
  base::MutexGuard guard(mutex());

  DCHECK(identity() == other->identity());

  // Move over pages.
  for (auto it = other->begin(); it != other->end();) {
    Page* p = *(it++);

      p->MergeOldToNewRememberedSets();

    // Ensure that pages are initialized before objects on it are discovered by
    // concurrent markers.
    p->InitializationMemoryFence();

    // Relinking requires the category to be unlinked.
    other->RemovePage(p);
    AddPage(p);
    DCHECK_IMPLIES(
        !p->IsFlagSet(Page::NEVER_ALLOCATE_ON_PAGE),
        p->AvailableInFreeList() == p->AvailableInFreeListFromAllocatedBytes());

    // TODO(leszeks): Here we should allocation step, but:
    //   1. Allocation groups are currently not handled properly by the sampling
    //      allocation profiler, and
    //   2. Observers might try to take the space lock, which isn't reentrant.
    // We'll have to come up with a better solution for allocation stepping
    // before shipping, which will likely be using LocalHeap.
  }
  for (auto p : other->GetNewPages()) {
    heap()->NotifyOldGenerationExpansion(identity(), p);
  }

  DCHECK_EQ(0u, other->Size());
  DCHECK_EQ(0u, other->Capacity());
}

size_t PagedSpace::CommittedPhysicalMemory() {
  if (!base::OS::HasLazyCommits()) return CommittedMemory();
  base::MutexGuard guard(mutex());
  if (main_thread_allocator_) {
    main_thread_allocator_->UpdateHighWaterMark();
  }
  size_t size = 0;
  for (Page* page : *this) {
    size += page->CommittedPhysicalMemory();
  }
  return size;
}

bool PagedSpace::ContainsSlow(Address addr) const {
  Page* p = Page::FromAddress(addr);
  for (const Page* page : *this) {
    if (page == p) return true;
  }
  return false;
}

void PagedSpace::RefineAllocatedBytesAfterSweeping(Page* page) {
  CHECK(page->SweepingDone());
  auto marking_state =
      heap()->incremental_marking()->non_atomic_marking_state();
  // The live_byte on the page was accounted in the space allocated
  // bytes counter. After sweeping allocated_bytes() contains the
  // accurate live byte count on the page.
  size_t old_counter = marking_state->live_bytes(page);
  size_t new_counter = page->allocated_bytes();
  DCHECK_GE(old_counter, new_counter);
  if (old_counter > new_counter) {
    DecreaseAllocatedBytes(old_counter - new_counter, page);
  }
  marking_state->SetLiveBytes(page, 0);
}

Page* PagedSpace::RemovePageSafe(int size_in_bytes) {
  base::MutexGuard guard(mutex());
  Page* page = free_list()->GetPageForSize(size_in_bytes);
  if (!page) return nullptr;
  RemovePage(page);
  return page;
}

size_t PagedSpace::AddPage(Page* page) {
  CHECK(page->SweepingDone());
  page->set_owner(this);
  memory_chunk_list_.PushBack(page);
  AccountCommitted(page->size());
  IncreaseCapacity(page->area_size());
  IncreaseAllocatedBytes(page->allocated_bytes(), page);
  for (size_t i = 0; i < ExternalBackingStoreType::kNumTypes; i++) {
    ExternalBackingStoreType t = static_cast<ExternalBackingStoreType>(i);
    IncrementExternalBackingStoreBytes(t, page->ExternalBackingStoreBytes(t));
  }
  return RelinkFreeListCategories(page);
}

void PagedSpace::RemovePage(Page* page) {
  CHECK(page->SweepingDone());
  memory_chunk_list_.Remove(page);
  UnlinkFreeListCategories(page);
  DecreaseAllocatedBytes(page->allocated_bytes(), page);
  DecreaseCapacity(page->area_size());
  AccountUncommitted(page->size());
  for (size_t i = 0; i < ExternalBackingStoreType::kNumTypes; i++) {
    ExternalBackingStoreType t = static_cast<ExternalBackingStoreType>(i);
    DecrementExternalBackingStoreBytes(t, page->ExternalBackingStoreBytes(t));
  }
}

size_t PagedSpace::ShrinkPageToHighWaterMark(Page* page) {
  if (main_thread_allocator_) {
    main_thread_allocator_->UpdateHighWaterMark();
  }
  size_t unused = page->ShrinkToHighWaterMark();
  accounting_stats_.DecreaseCapacity(static_cast<intptr_t>(unused));
  AccountUncommitted(unused);
  return unused;
}

void PagedSpace::ResetFreeList() {
  for (Page* page : *this) {
    free_list_->EvictFreeListItems(page);
  }
  DCHECK(free_list_->IsEmpty());
}

void PagedSpace::ShrinkImmortalImmovablePages() {
  DCHECK(!heap()->deserialization_complete());
  // BasicMemoryChunk::UpdateHighWaterMark(allocation_info_.top());
  ResetFreeList();
  for (Page* page : *this) {
    DCHECK(page->IsFlagSet(Page::NEVER_EVACUATE));
    ShrinkPageToHighWaterMark(page);
  }
}

Page* PagedSpace::AllocatePage() {
  return heap()->memory_allocator()->AllocatePage(AreaSize(), this,
                                                  executable());
}

Page* PagedSpace::Expand() {
  Page* page = AllocatePage();
  if (page == nullptr) return nullptr;
  base::MutexGuard lock(&space_mutex_);
  AddPage(page);
  Free(page->area_start(), page->area_size(),
       SpaceAccountingMode::kSpaceAccounted);
  return page;
}

int PagedSpace::CountTotalPages() {
  int count = 0;
  for (Page* page : *this) {
    count++;
    USE(page);
  }
  return count;
}

size_t PagedSpace::Available() {
  base::MutexGuard lock(&space_mutex_);
  return free_list_->Available();
}

void PagedSpace::ReleasePage(Page* page) {
  DCHECK_EQ(
      0, heap()->incremental_marking()->non_atomic_marking_state()->live_bytes(
             page));
  DCHECK_EQ(page->owner(), this);

  free_list_->EvictFreeListItems(page);

  if (identity() == CODE_SPACE) {
    heap()->isolate()->RemoveCodeMemoryChunk(page);
  }

  AccountUncommitted(page->size());
  accounting_stats_.DecreaseCapacity(page->area_size());
  heap()->memory_allocator()->Free<MemoryAllocator::kPreFreeAndQueue>(page);
}

void PagedSpace::SetReadable() {
  DCHECK(identity() == CODE_SPACE);
  for (Page* page : *this) {
    CHECK(heap()->memory_allocator()->IsMemoryChunkExecutable(page));
    page->SetReadable();
  }
}

void PagedSpace::SetReadAndExecutable() {
  DCHECK(identity() == CODE_SPACE);
  for (Page* page : *this) {
    CHECK(heap()->memory_allocator()->IsMemoryChunkExecutable(page));
    page->SetReadAndExecutable();
  }
}

void PagedSpace::SetReadAndWritable() {
  DCHECK(identity() == CODE_SPACE);
  for (Page* page : *this) {
    CHECK(heap()->memory_allocator()->IsMemoryChunkExecutable(page));
    page->SetReadAndWritable();
  }
}

std::unique_ptr<ObjectIterator> PagedSpace::GetObjectIterator(Heap* heap) {
  return std::unique_ptr<ObjectIterator>(
      new PagedSpaceObjectIterator(heap, this));
}

bool PagedSpace::RefillLabFromFreeList(size_t min_size, size_t max_size,
                                       AllocationOrigin origin, Address* top,
                                       Address* limit) {
  {
    base::MutexGuard lock(&space_mutex_);
    FreeLabImpl(top, limit);
    DCHECK_LE(min_size, max_size);
    size_t new_node_size = 0;
    FreeSpace new_node = free_list_->Allocate(min_size, &new_node_size, origin);
    if (new_node.is_null()) return false;
    DCHECK_GE(new_node_size, min_size);

    Page* page = Page::FromHeapObject(new_node);
    IncreaseAllocatedBytes(new_node_size, page);

    if (new_node_size > max_size) {
      Free(new_node.address() + max_size, new_node_size - max_size,
           SpaceAccountingMode::kSpaceAccounted);
      new_node_size = max_size;
    }

    *top = new_node.address();
    *limit = new_node.address() + new_node_size;
  }
  if (heap()->incremental_marking()->black_allocation()) {
    Page::FromAllocationAreaAddress(*top)->CreateBlackAreaBackground(*top,
                                                                     *limit);
  }
  return true;
}

#ifdef DEBUG
void PagedSpace::Print() {}
#endif

#ifdef VERIFY_HEAP
void PagedSpace::Verify(Isolate* isolate, ObjectVisitor* visitor) {
  size_t external_space_bytes[kNumTypes];
  size_t external_page_bytes[kNumTypes];

  for (int i = 0; i < kNumTypes; i++) {
    external_space_bytes[static_cast<ExternalBackingStoreType>(i)] = 0;
  }

  for (Page* page : *this) {
    CHECK_EQ(page->owner(), this);

    for (int i = 0; i < kNumTypes; i++) {
      external_page_bytes[static_cast<ExternalBackingStoreType>(i)] = 0;
    }

    CHECK(page->SweepingDone());
    PagedSpaceObjectIterator it(isolate->heap(), this, page);
    Address end_of_previous_object = page->area_start();
    Address top = page->area_end();

    for (HeapObject object = it.Next(); !object.is_null(); object = it.Next()) {
      CHECK(end_of_previous_object <= object.address());

      // The first word should be a map, and we expect all map pointers to
      // be in map space.
      Map map = object.map();
      CHECK(map.IsMap());
      CHECK(ReadOnlyHeap::Contains(map) ||
            isolate->heap()->map_space()->Contains(map));

      // Perform space-specific object verification.
      VerifyObject(object);

      // The object itself should look OK.
      object.ObjectVerify(isolate);

      if (identity() != RO_SPACE && !FLAG_verify_heap_skip_remembered_set) {
        isolate->heap()->VerifyRememberedSetFor(object);
      }

      // All the interior pointers should be contained in the heap.
      int size = object.Size();
      object.IterateBody(map, size, visitor);
      CHECK(object.address() + size <= top);
      end_of_previous_object = object.address() + size;

      if (object.IsExternalString()) {
        ExternalString external_string = ExternalString::cast(object);
        size_t size = external_string.ExternalPayloadSize();
        external_page_bytes[ExternalBackingStoreType::kExternalString] += size;
      }
    }
    for (int i = 0; i < kNumTypes; i++) {
      ExternalBackingStoreType t = static_cast<ExternalBackingStoreType>(i);
      CHECK_EQ(external_page_bytes[t], page->ExternalBackingStoreBytes(t));
      external_space_bytes[t] += external_page_bytes[t];
    }
  }
  for (int i = 0; i < kNumTypes; i++) {
    if (i == ExternalBackingStoreType::kArrayBuffer) continue;
    ExternalBackingStoreType t = static_cast<ExternalBackingStoreType>(i);
    CHECK_EQ(external_space_bytes[t], ExternalBackingStoreBytes(t));
  }

  if (identity() == OLD_SPACE) {
    size_t bytes = heap()->array_buffer_sweeper()->old().BytesSlow();
    CHECK_EQ(bytes,
             ExternalBackingStoreBytes(ExternalBackingStoreType::kArrayBuffer));
  }

#ifdef DEBUG
  VerifyCountersAfterSweeping(isolate->heap());
#endif
}

void PagedSpace::VerifyLiveBytes() {
  IncrementalMarking::MarkingState* marking_state =
      heap()->incremental_marking()->marking_state();
  for (Page* page : *this) {
    CHECK(page->SweepingDone());
    PagedSpaceObjectIterator it(heap(), this, page);
    int black_size = 0;
    for (HeapObject object = it.Next(); !object.is_null(); object = it.Next()) {
      // All the interior pointers should be contained in the heap.
      if (marking_state->IsBlack(object)) {
        black_size += object.Size();
      }
    }
    CHECK_LE(black_size, marking_state->live_bytes(page));
  }
}
#endif  // VERIFY_HEAP

#ifdef DEBUG
void PagedSpace::VerifyCountersAfterSweeping(Heap* heap) {
  size_t total_capacity = 0;
  size_t total_allocated = 0;
  for (Page* page : *this) {
    DCHECK(page->SweepingDone());
    total_capacity += page->area_size();
    PagedSpaceObjectIterator it(heap, this, page);
    size_t real_allocated = 0;
    for (HeapObject object = it.Next(); !object.is_null(); object = it.Next()) {
      if (!object.IsFreeSpaceOrFiller()) {
        real_allocated += object.Size();
      }
    }
    total_allocated += page->allocated_bytes();
    // The real size can be smaller than the accounted size if array trimming,
    // object slack tracking happened after sweeping.
    DCHECK_LE(real_allocated, accounting_stats_.AllocatedOnPage(page));
    DCHECK_EQ(page->allocated_bytes(), accounting_stats_.AllocatedOnPage(page));
  }
  DCHECK_EQ(total_capacity, accounting_stats_.Capacity());
  DCHECK_EQ(total_allocated, accounting_stats_.Size());
}

void PagedSpace::VerifyCountersBeforeConcurrentSweeping() {
  // We need to refine the counters on pages that are already swept and have
  // not been moved over to the actual space. Otherwise, the AccountingStats
  // are just an over approximation.
  RefillFreeList();

  size_t total_capacity = 0;
  size_t total_allocated = 0;
  auto marking_state =
      heap()->incremental_marking()->non_atomic_marking_state();
  for (Page* page : *this) {
    size_t page_allocated =
        page->SweepingDone()
            ? page->allocated_bytes()
            : static_cast<size_t>(marking_state->live_bytes(page));
    total_capacity += page->area_size();
    total_allocated += page_allocated;
    DCHECK_EQ(page_allocated, accounting_stats_.AllocatedOnPage(page));
  }
  DCHECK_EQ(total_capacity, accounting_stats_.Capacity());
  DCHECK_EQ(total_allocated, accounting_stats_.Size());
}
#endif

// -----------------------------------------------------------------------------
// OldSpace implementation

void PagedSpace::PrepareForMarkCompact() {
  // Clear the free list before a full GC---it will be rebuilt afterward.
  free_list_->Reset();
}

Page* LocalSpace::Expand() {
  Page* page = PagedSpace::Expand();
  new_pages_.push_back(page);
  return page;
}

bool PagedSpace::TryExpand(ThreadKind thread_kind) {
  Page* page = Expand();
  if (!page) return false;
  if (thread_kind == ThreadKind::kMain) {
    DCHECK(!is_compaction_space());
    heap()->NotifyOldGenerationExpansion(identity(), page);
  }
  return true;
}

bool PagedSpace::ContributeToSweeping(size_t required_freed_bytes,
                                      int max_pages) {
  // Cleanup invalidated old-to-new refs for compaction space in the
  // final atomic pause.
  Sweeper::FreeSpaceMayContainInvalidatedSlots invalidated_slots_in_free_space =
      is_compaction_space() ? Sweeper::FreeSpaceMayContainInvalidatedSlots::kYes
                            : Sweeper::FreeSpaceMayContainInvalidatedSlots::kNo;

  MarkCompactCollector* collector = heap()->mark_compact_collector();
  if (collector->sweeping_in_progress()) {
    collector->sweeper()->ParallelSweepSpace(
        identity(), static_cast<int>(required_freed_bytes), max_pages,
        invalidated_slots_in_free_space);
    RefillFreeList();
    return true;
  }
  return false;
}

bool PagedSpace::RefillLab(ThreadKind thread_kind, size_t min_size,
                           size_t max_size, AllocationAlignment alignment,
                           AllocationOrigin origin,
                           HeapLimitHandling heap_limit_handling, Address* top,
                           Address* limit, AllocationFailure* failure) {
  size_t max_alignment = Heap::GetMaximumFillToAlign(alignment);
  min_size += max_alignment;
  max_size = std::max(min_size, max_size);

  if (heap_limit_handling == HeapLimitHandling::kRespect &&
      !heap()->CheckIncrementalMarkingLimitOnSlowAllocation(thread_kind)) {
    base::MutexGuard lock(&space_mutex_);
    FreeLabImpl(top, limit);
    *failure = AllocationFailure::kRetryAfterIncrementalMarkingStart;
    return false;
  }

  if (RefillLabFromFreeList(min_size, max_size, origin, top, limit)) {
    return true;
  }

  MarkCompactCollector* collector = heap()->mark_compact_collector();
  // Sweeping is still in progress.
  if (collector->sweeping_in_progress()) {
    // First try to refill the free-list, concurrent sweeper threads
    // may have freed some objects in the meantime.
    RefillFreeList();

    if (RefillLabFromFreeList(min_size, max_size, origin, top, limit)) {
      return true;
    }
    const int kMaxPagesToSweep = 1;
    if (ContributeToSweeping(min_size, kMaxPagesToSweep) &&
        RefillLabFromFreeList(min_size, max_size, origin, top, limit)) {
      return true;
    }
  }
  if (is_compaction_space()) {
    // The main thread may have acquired all swept pages. Try to steal from
    // it. This can only happen during young generation evacuation.
    PagedSpace* main_space = heap()->paged_space(identity());
    Page* page = main_space->RemovePageSafe(static_cast<int>(min_size));
    if (page != nullptr) {
      AddPage(page);
      if (RefillLabFromFreeList(min_size, max_size, origin, top, limit)) {
        return true;
      }
    }
  }

  if (heap_limit_handling != HeapLimitHandling::kRespect ||
      heap()->ShouldExpandOldGenerationOnSlowAllocation(thread_kind)) {
    if (heap()->CanExpandOldGeneration(thread_kind, AreaSize())) {
      if (TryExpand(thread_kind) &&
          RefillLabFromFreeList(min_size, max_size, origin, top, limit)) {
        return true;
      }
    }
  }

  // Try sweeping all pages.
  if (ContributeToSweeping(0, 0) &&
      RefillLabFromFreeList(min_size, max_size, origin, top, limit)) {
    return true;
  }

  if (heap_limit_handling == HeapLimitHandling::kIgnore &&
      !heap()->force_oom()) {
    if (TryExpand(thread_kind) &&
        RefillLabFromFreeList(min_size, max_size, origin, top, limit)) {
      return true;
    }
  }
  *top = kNullAddress;
  *limit = kNullAddress;
  *failure = AllocationFailure::kRetryAfterFullGC;
  return false;
}

void PagedSpace::FreeLab(ThreadKind, Address* top, Address* limit) {
  base::MutexGuard lock(&space_mutex_);
  base::Optional<CodePageCollectionMemoryModificationScope> scope;
  if (identity() == CODE_SPACE && *top != *limit) {
    scope.emplace(heap_);
  }
  FreeLabImpl(top, limit);
}

void PagedSpace::FreeLabImpl(Address* top, Address* limit) {
  BasicMemoryChunk::UpdateHighWaterMark(*top);
  if (*top != *limit) {
    if (heap()->incremental_marking()->black_allocation()) {
      Page::FromAddress(*top)->DestroyBlackAreaBackground(*top, *limit);
    }
    if (identity() == CODE_SPACE) {
      heap()->UnprotectAndRegisterMemoryChunk(MemoryChunk::FromAddress(*top));
    }
    Free(*top, *limit - *top, SpaceAccountingMode::kSpaceAccounted);
  }
  *top = kNullAddress;
  *limit = kNullAddress;
}

void PagedSpace::StartBlackAllocation(Address top, Address limit) {
  Page::FromAddress(top)->CreateBlackAreaBackground(top, limit);
}

void PagedSpace::StopBlackAllocation(Address top, Address limit) {
  Page::FromAddress(top)->DestroyBlackAreaBackground(top, limit);
}

// -----------------------------------------------------------------------------
// MapSpace implementation

// TODO(dmercadier): use a heap instead of sorting like that.
// Using a heap will have multiple benefits:
//   - for now, SortFreeList is only called after sweeping, which is somewhat
//   late. Using a heap, sorting could be done online: FreeListCategories would
//   be inserted in a heap (ie, in a sorted manner).
//   - SortFreeList is a bit fragile: any change to FreeListMap (or to
//   MapSpace::free_list_) could break it.
void MapSpace::SortFreeList() {
  using LiveBytesPagePair = std::pair<size_t, Page*>;
  std::vector<LiveBytesPagePair> pages;
  pages.reserve(CountTotalPages());

  for (Page* p : *this) {
    free_list()->RemoveCategory(p->free_list_category(kFirstCategory));
    pages.push_back(std::make_pair(p->allocated_bytes(), p));
  }

  // Sorting by least-allocated-bytes first.
  std::sort(pages.begin(), pages.end(),
            [](const LiveBytesPagePair& a, const LiveBytesPagePair& b) {
              return a.first < b.first;
            });

  for (LiveBytesPagePair const& p : pages) {
    // Since AddCategory inserts in head position, it reverts the order produced
    // by the sort above: least-allocated-bytes will be Added first, and will
    // therefore be the last element (and the first one will be
    // most-allocated-bytes).
    free_list()->AddCategory(p.second->free_list_category(kFirstCategory));
  }
}

#ifdef VERIFY_HEAP
void MapSpace::VerifyObject(HeapObject object) { CHECK(object.IsMap()); }
#endif

}  // namespace internal
}  // namespace v8
