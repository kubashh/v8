// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles/handles.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using HandleScopesTest = TestWithIsolate;

namespace {

class CounterVisitor : public RootVisitor {
 public:
  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    counter += end - start;
  }
  size_t counter = 0;
};

size_t count_handles(HandleScopeImplementer* hsi) {
  CounterVisitor visitor;
  hsi->Iterate(&visitor);
  return visitor.counter;
}

void CheckNumHandles(Isolate* isolate, int expected) {
  HandleScopeImplementer* hsi = isolate->handle_scope_implementer();
  CHECK_EQ(hsi->blocks()->size(), expected / kHandleBlockSize + 1);
  CHECK_EQ(count_handles(hsi), expected);
  CHECK_EQ(HandleScope::NumberOfHandles(isolate), expected);
}

}  // namespace

TEST_F(HandleScopesTest, TestCreateHandle) {
  Isolate* isolate = i_isolate();
  Heap* heap = isolate->heap();
  CheckNumHandles(isolate, 0);
  {
    HandleScope scope(isolate);
    handle(ReadOnlyRoots(heap).empty_string(), isolate);
    CheckNumHandles(isolate, 1);
  }
  CheckNumHandles(isolate, 0);
}

TEST_F(HandleScopesTest, TestFullBlock) {
  Isolate* isolate = i_isolate();
  Heap* heap = isolate->heap();
  CheckNumHandles(isolate, 0);
  {
    HandleScope scope(isolate);
    for (int i = 0; i < kHandleBlockSize; i++) {
      handle(ReadOnlyRoots(heap).empty_string(), isolate);
    }
    CheckNumHandles(isolate, kHandleBlockSize);
  }
  CheckNumHandles(isolate, 0);
}

TEST_F(HandleScopesTest, TestExtendWhenFull) {
  Isolate* isolate = i_isolate();
  Heap* heap = isolate->heap();
  CheckNumHandles(isolate, 0);
  {
    HandleScope scope(isolate);
    for (int i = 0; i < kHandleBlockSize; i++) {
      handle(ReadOnlyRoots(heap).empty_string(), isolate);
    }
    CheckNumHandles(isolate, kHandleBlockSize);
    handle(ReadOnlyRoots(heap).empty_string(), isolate);
    CheckNumHandles(isolate, kHandleBlockSize + 1);
  }

  CheckNumHandles(isolate, 0);
}

TEST_F(HandleScopesTest, TestExtendWhenFullNested) {
  Isolate* isolate = i_isolate();
  Heap* heap = isolate->heap();
  CheckNumHandles(isolate, 0);
  {
    HandleScope scope(isolate);
    for (int i = 0; i < kHandleBlockSize; i++) {
      handle(ReadOnlyRoots(heap).empty_string(), isolate);
    }
    CheckNumHandles(isolate, kHandleBlockSize);
    {
      HandleScope scope(isolate);
      for (int j = 0; j < kHandleBlockSize; j++) {
        handle(ReadOnlyRoots(heap).empty_string(), isolate);
      }
      CheckNumHandles(isolate, kHandleBlockSize * 2);
      {
        HandleScope scope(isolate);
        handle(ReadOnlyRoots(heap).empty_string(), isolate);
        CheckNumHandles(isolate, kHandleBlockSize * 2 + 1);
      }
      CheckNumHandles(isolate, kHandleBlockSize * 2);
    }
    CheckNumHandles(isolate, kHandleBlockSize);
  }
  CheckNumHandles(isolate, 0);
}

}  // namespace internal
}  // namespace v8
