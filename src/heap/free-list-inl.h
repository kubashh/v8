// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_FREE_LIST_INL_H_
#define V8_HEAP_FREE_LIST_INL_H_

#include "src/heap/free-list.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

bool FreeListCategory::is_linked(FreeList* owner) const {
  return prev_ != nullptr || next_ != nullptr ||
         owner->categories_[type_] == this;
}

void FreeListCategory::UpdateCountersAfterAllocation(size_t allocation_size) {
  available_ -= allocation_size;
}

Page* FreeList::GetPageForCategoryType(FreeListCategoryType type) {
  FreeListCategory* category_top = top(type);
  if (category_top != nullptr) {
    DCHECK(!category_top->top().is_null());
    return Page::FromHeapObject(category_top->top());
  } else {
    return nullptr;
  }
}

// ------------------------------------------------
// FreeListManyCachedFastPathBase implementation

template <bool allow_small_blocks>
FreeSpace FreeListManyCachedFastPathBase<allow_small_blocks>::Allocate(
    size_t size_in_bytes, size_t* node_size, AllocationOrigin origin) {
  USE(origin);
  DCHECK_GE(kMaxBlockSize, size_in_bytes);
  FreeSpace node;

  // Fast path part 1: searching the last categories
  FreeListCategoryType first_category =
      SelectFastAllocationFreeListCategoryType(size_in_bytes);
  FreeListCategoryType type = first_category;
  for (type = next_nonempty_category[type]; type <= last_category_;
       type = next_nonempty_category[type + 1]) {
    node = TryFindNodeIn(type, size_in_bytes, node_size);
    if (!node.is_null()) break;
  }

  // Fast path part 2: searching the medium categories for tiny objects
  if (allow_small_blocks) {
    if (node.is_null()) {
      if (size_in_bytes <= kTinyObjectMaxSize) {
        DCHECK_EQ(kFastPathFirstCategory, first_category);
        for (type = next_nonempty_category[kFastPathFallBackTiny];
             type < kFastPathFirstCategory;
             type = next_nonempty_category[type + 1]) {
          node = TryFindNodeIn(type, size_in_bytes, node_size);
          if (!node.is_null()) break;
        }
        first_category = kFastPathFallBackTiny;
      }
    }
  }

  // Searching the last category
  if (node.is_null()) {
    // Searching each element of the last category.
    type = last_category_;
    node = SearchForNodeInList(type, size_in_bytes, node_size);
  }

  // Finally, search the most precise category
  if (node.is_null()) {
    type = SelectFreeListCategoryType(size_in_bytes);
    for (type = next_nonempty_category[type]; type < first_category;
         type = next_nonempty_category[type + 1]) {
      node = TryFindNodeIn(type, size_in_bytes, node_size);
      if (!node.is_null()) break;
    }
  }

  if (!node.is_null()) {
    if (categories_[type] == nullptr) UpdateCacheAfterRemoval(type);
    Page::FromHeapObject(node)->IncreaseAllocatedBytes(*node_size);
  }

#ifdef DEBUG
  CheckCacheIntegrity();
#endif

  DCHECK(IsVeryLong() || Available() == SumFreeLists());
  return node;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_FREE_LIST_INL_H_
