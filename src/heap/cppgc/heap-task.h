// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_HEAP_TASK_H_
#define V8_HEAP_CPPGC_HEAP_TASK_H_

#include <cstdint>

#include "include/cppgc/platform.h"
#include "src/base/macros.h"

namespace cppgc {
namespace internal {

class V8_EXPORT_PRIVATE HeapTask {
 public:
  enum class ExecutionType : uint8_t { kAtomic, kConcurrent };

  // Template methods.
  void Start(ExecutionType);
  void Finish();

 protected:
  explicit HeapTask(Platform*);

  virtual void DoStartAtomic() = 0;
  virtual void DoStartConcurrent() = 0;

  virtual void DoFinish() = 0;
  virtual void DidSynchronizeConcurrentTask() = 0;

 private:
  Platform* platform_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_HEAP_TASK_H_
