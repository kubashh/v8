// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/microtask-queue.h"

#include "src/objects/microtask-queue-inl.h"

namespace v8 {
namespace internal {

void MicrotaskQueue::EnqueueMicrotask(Handle<Microtask> microtask) {
  UNIMPLEMENTED();
}

void MicrotaskQueue::RunMicrotasks() { UNIMPLEMENTED(); }

}  // namespace internal
}  // namespace v8
