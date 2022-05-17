// Copyright 2022 the V8 project authors. All rights reserved.
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
  range->reserved_ = std::move(reservation);
  return range;
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
}  // namespace internal
}  // namespace v8
