// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/setup-isolate-for-tests.h"

// Almost identical to setup-isolate-full.cc

namespace v8 {
namespace internal {

void SetupIsolateDelegateForTests::SetupBuiltins(Isolate* isolate) {
  SetupBuiltinsInternal(isolate);
}

bool SetupIsolateDelegateForTests::SetupHeap(Heap* heap) {
  return SetupHeapInternal(heap);
}

void SetupIsolateDelegateForTests::SetupFromSnapshot(Isolate* isolate) {
  // In testing embedded snapshot blob can be missing
}

}  // namespace internal
}  // namespace v8
