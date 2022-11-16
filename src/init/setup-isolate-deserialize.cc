// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/logging.h"
#include "src/execution/isolate.h"
#include "src/init/setup-isolate.h"

namespace v8 {
namespace internal {

void SetupIsolateDelegate::SetupBuiltinPlaceholders(Isolate* isolate) {
  FATAL("Builtin compilation supported only in mksnapshot");
}

void SetupIsolateDelegate::CompileBuiltins(Isolate* isolate) {
  FATAL("Builtin compilation supported only in mksnapshot");
}

bool SetupIsolateDelegate::SetupHeap(Heap* heap) {
  FATAL("Heap setup supported only in mksnapshot");
}

void SetupIsolateDelegate::SetupFromSnapshot(Isolate* isolate) {
  DCHECK(isolate->snapshot_available());
}

}  // namespace internal
}  // namespace v8
