// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/huge-page-range.h"

#include "src/heap/memory-allocator.h"
#include "src/heap/memory-chunk.h"

namespace v8 {
namespace internal {

// static
HugePageRange* HugePageRange::Initialize(Heap* heap,
                                         VirtualMemory reservation) {
  HugePageRange* range = new HugePageRange();
  range->heap_ = heap;
  bool success = reservation.AdviseHugePage();
  if (!success) {
    reservation.Free();
    delete range;
    return nullptr;
  }
  range->reserved_ = std::move(reservation);
  return range;
}

void HugePageRange::Reset() {
  page_num_ = 0;
  page_bitmap_ = 0;
}

Address HugePageRange::Allocate() {
  size_t index = FirstAllocatableIndex();
  if (V8_UNLIKELY(index == kMaxPageNum)) {
    return kNullAddress;
  }
  Address base = AddressFromIndex(index);

  SetBitMap(index, true);
  page_num_++;
  return base;
}

void HugePageRange::Remove(BasicMemoryChunk* chunk) {
  size_t index = ChunkIndex(chunk);
  page_num_--;
  SetBitMap(index, false);
}

size_t HugePageRange::FirstAllocatableIndex() {
  for (size_t index = 0; index < kMaxPageNum; ++index) {
    if (!Contains(index)) {
      return index;
    }
  }
  // No allocatable slot remain.
  return kMaxPageNum;
}

size_t HugePageRange::ChunkIndex(BasicMemoryChunk* chunk) {
  Address chunk_base = chunk->address();
  size_t offset = static_cast<size_t>(chunk_base - address());
  size_t index = static_cast<size_t>(offset / Page::kPageSize);
  return index;
}

HugePageRange::HugePageRange() : page_num_(0), page_bitmap_(0x00) {}

size_t HugePageRangeManager::HugePageRangeNum() {
  DCHECK(CheckRangeNum());
  return range_num_;
}

Page* HugePageRangeManager::TryAllocatePageInHugePageRange(Space* owner) {
  base::MutexGuard guard(&mutex_);
  HugePageRange* range = FetchHugePageRange(owner->identity());
  if (!range) {
    return nullptr;
  }

  Address base = range->Allocate();
  ranges_[range->page_num()].push_back(range);
  range_num_++;
  DCHECK(CheckRangeNum());

  if (V8_UNLIKELY(!base)) {
    return nullptr;
  }
  MemoryAllocator* memory_allocator = heap_->memory_allocator();
  size_t size = Page::kPageSize;
  if (Heap::ShouldZapGarbage()) {
    memory_allocator->ZapBlock(base, size, kZapValue);
  }
  Address area_start = base + MemoryChunkLayout::ObjectStartOffsetInDataPage();
  Address area_end = base + size;
  VirtualMemory reservation(memory_allocator->page_allocator(NOT_EXECUTABLE),
                            base, MemoryChunk::kPageSize);
  BasicMemoryChunk* basic_chunk = BasicMemoryChunk::Initialize(
      heap_, base, MemoryChunk::kPageSize, area_start, area_end, owner,
      std::move(reservation));

  MemoryChunk* chunk =
      MemoryChunk::Initialize(basic_chunk, heap_, NOT_EXECUTABLE);
  Page* page = owner->InitializePage(chunk);
  page->set_huge_page(range);

  return page;
}

HugePageRange* HugePageRangeManager::TryFreePageInHugePageRange(
    MemoryChunk* chunk) {
  base::MutexGuard guard(&mutex_);
  HugePageRange* range = chunk->huge_page();
  CHECK(range);
  size_t range_page_num = range->page_num();
  range->Remove(chunk);
  ranges_[range_page_num].remove(range);
  range_num_--;
  DCHECK(CheckRangeNum());
  range_page_num--;
  if (range_page_num == 0) {
    delete range;
  } else {
    ranges_[range_page_num].push_back(range);
    range_num_++;
    DCHECK(CheckRangeNum());
  }
  return range;
}

HugePageRange* HugePageRangeManager::CreateHugePageRange() {
  return heap_->memory_allocator()->AllocateHugePageRange();
}

HugePageRange* HugePageRangeManager::FetchHugePageRange(
    AllocationSpace identity) {
  for (int i = HugePageRange::kMaxPageNum - 1; i >= 0; --i) {
    if (ranges_[i].size() > 0) {
      HugePageRange* range = ranges_[i].back();
      ranges_[i].pop_back();
      range_num_--;
      DCHECK(CheckRangeNum());
      return range;
    }
  }

  if (identity != NEW_SPACE && range_num_ < heap_->max_huge_page_range()) {
    return CreateHugePageRange();
  }
  return nullptr;
}

#ifdef DEBUG
bool HugePageRangeManager::CheckRangeNum() {
  size_t num = 0;
  for (size_t i = 0; i <= HugePageRange::kMaxPageNum; ++i) {
    num += ranges_[i].size();
  }
  return num == range_num_;
}
#endif

}  // namespace internal
}  // namespace v8
