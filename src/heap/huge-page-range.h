// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HUGE_PAGE_RANGE_H_
#define V8_HEAP_HUGE_PAGE_RANGE_H_

#include "src/base/build_config.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {
class HugePageRange {
 public:
  static const size_t kHugeRangeSize = 1U << kHugePageSizeBits;

  static const size_t kMaxPageNum = kHugeRangeSize / Page::kPageSize;

  static HugePageRange* Initialize(Heap* heap, VirtualMemory reservation);

  static const intptr_t kAlignment =
      (static_cast<uintptr_t>(1) << kHugePageSizeBits);

  static const intptr_t kAlignmentMask = kAlignment - 1;

  static Address BaseAddress(Address a) { return a & ~kAlignmentMask; }

  static HugePageRange* FromAddress(Heap* heap, Address a);

  static HugePageRange* FromBasicMemoryChunk(BasicMemoryChunk* chunk);

  HugePageRange();

  size_t page_num() { return page_num_; }

  VirtualMemory* reserved_memory() { return &reserved_; }

  Address address() { return reserved_.address(); }

  size_t size() { return reserved_.size(); }

  bool empty() { return page_num_ == 0; }

  void Reset();

  void Allocate(size_t index);

  void Remove(BasicMemoryChunk* chunk);

  inline bool Contains(size_t index) {
    uint8_t mask = 1 << index;
    return page_bitmap_ & mask;
  }

  inline void SetBitMap(size_t index, bool has_page) {
    uint8_t mask = 1 << index;
    if (has_page) {
      page_bitmap_ |= mask;
    } else {
      mask = ~mask;
      page_bitmap_ &= mask;
    }
  }

  Heap* heap() { return heap_; }

  size_t FirstAllocatableIndex();

  inline Address AddressFromIndex(size_t index) {
    return address() + index * Page::kPageSize;
  }

  size_t ChunkIndex(BasicMemoryChunk* chunk);

 protected:
  size_t page_num_ = 0;
  VirtualMemory reserved_;
  Heap* heap_;

  uint8_t page_bitmap_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HUGE_PAGE_RANGE_H_
