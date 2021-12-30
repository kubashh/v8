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

// static
HugePageRange* HugePageRange::FromAddress(Heap* heap, Address a) {
  DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
  for (HugePageRange* range : heap->huge_page_range_manager()->ranges()) {
    if (range->address() == a) {
      return range;
    }
  }
  return nullptr;
}

// static
HugePageRange* HugePageRange::FromBasicMemoryChunk(BasicMemoryChunk* chunk) {
  HugePageRange* range =
      FromAddress(chunk->heap(), BaseAddress(chunk->address()));
  return range;
}

void HugePageRange::Reset() {
  page_num_ = 0;
  page_bitmap_ = 0;
}

Address HugePageRange::Allocate() {
  base::MutexGuard guard(&mutex_);
  size_t index = FirstAllocatableIndex();
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
  UNREACHABLE();
}

size_t HugePageRange::ChunkIndex(BasicMemoryChunk* chunk) {
  Address chunk_base = chunk->address();
  size_t offset = static_cast<size_t>(chunk_base - address());
  size_t index = static_cast<size_t>(offset / Page::kPageSize);
  return index;
}

HugePageRange::HugePageRange() : page_num_(0), page_bitmap_(0x00) {}

HugePageRange* HugePageRangeManager::FetchHugePageRange() {
  {
    base::MutexGuard guard(&mutex_);
    for (HugePageRange* range : ranges_) {
      if (range->page_num() < HugePageRange::kMaxPageNum) {
        return range;
      }
    }
  }
  if (HugePageRangeNum() < heap_->max_huge_page_range()) {
    return CreateHugePageRange();
  }
  return nullptr;
}

void HugePageRangeManager::RemoveHugePageRange(HugePageRange* range) {
  base::MutexGuard guard(&mutex_);
  ranges_.remove(range);
}

HugePageRange* HugePageRangeManager::CreateHugePageRange() {
  HugePageRange* range = allocator_->AllocateHugePageRange();
  if (V8_LIKELY(range)) {
    base::MutexGuard guard(&mutex_);
    ranges_.push_back(range);
  }
  return range;
}

}  // namespace internal
}  // namespace v8
