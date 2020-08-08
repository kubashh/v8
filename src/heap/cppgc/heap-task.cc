// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap-task.h"

namespace cppgc {
namespace internal {

HeapTask::HeapTask(Platform* platform) : platform_(platform) {}

}  // namespace internal
}  // namespace cppgc
