// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HUGE_PAGE_RANGE_H_
#define V8_HEAP_HUGE_PAGE_RANGE_H_

#include <list>

#include "src/base/build_config.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

/*
The HugePageRange manages a 2MB reserved memory which map to a physical
huge page. It supports v8 page be allocated in the reserved memory.
*/
class HugePageRange {
 public:
  // The size of HugePageRange.
  static const size_t kHugeRangeSize = 1U << kHugePageBits;
  // Max number of page can be hold in a HugePageRange.
  static const size_t kMaxPageNum = kHugeRangeSize / Page::kPageSize;

  // Initialize a HugePageRange from VirtualMemory.
  static HugePageRange* Initialize(Heap* heap, VirtualMemory reservation);

  HugePageRange();

  size_t page_num() { return page_num_; }

  VirtualMemory* reserved_memory() { return &reserved_; }

  Address address() { return reserved_.address(); }

  size_t size() { return reserved_.size(); }

  bool empty() { return page_num_ == 0; }

  Address Allocate();

  void Remove(BasicMemoryChunk* chunk);

  Heap* heap() { return heap_; }

 private:
  // Whether this slot already hold an active page.
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

  // Get the index of the first allocatable slot.
  size_t FirstAllocatableIndex();

  // Get the base address of a slot
  inline Address AddressFromIndex(size_t index) {
    return address() + index * Page::kPageSize;
  }

  // Get the index of the slot that holding the chunk.
  size_t ChunkIndex(BasicMemoryChunk* chunk);

  // Current active page number in this HugePageRange.
  std::atomic<size_t> page_num_;
  VirtualMemory reserved_;
  Heap* heap_;
  // Each bit represent a slot for page, 1 for holding active page.
  std::atomic<uint8_t> page_bitmap_;
};
}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HUGE_PAGE_RANGE_H_
