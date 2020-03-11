// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/local-heap.h"
#include "src/heap/heap.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using LocalHeapTest = TestWithIsolate;

TEST_F(LocalHeapTest, Initialize) {
  Heap* heap = i_isolate()->heap();

  {
    LocalHeap lh1(heap);
    CHECK_EQ(heap->local_heaps_head(), &lh1);
    LocalHeap lh2(heap);
    CHECK_EQ(heap->local_heaps_head(), &lh2);

    {
      LocalHeap lh3(heap);
      CHECK_EQ(heap->local_heaps_head(), &lh3);
    }

    CHECK_EQ(heap->local_heaps_head(), &lh2);
  }

  CHECK_NULL(heap->local_heaps_head());
}

}  // namespace internal
}  // namespace v8
