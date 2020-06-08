// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_EXTERNAL_POINTER_INL_H_
#define V8_COMMON_EXTERNAL_POINTER_INL_H_

#include "include/v8-internal.h"
#include "src/common/external-pointer.h"
#include "src/execution/isolate.h"

namespace v8 {
namespace internal {

V8_INLINE ExternalPointer_t EncodeExternalPointer(Isolate* isolate,
                                                  Address external_pointer) {
  STATIC_ASSERT(kExternalPointerSize == kSystemPointerSize);
#ifdef V8_HEAP_SANDBOX
  if (V8_HEAP_SANDBOX_BOOL) {
    ExternalPointer_t idx = isolate->external_pointer_table().allocate();
    isolate->external_pointer_table().set(idx, external_pointer);
    return idx;
  }
#endif
  return external_pointer;
}

V8_INLINE Address DecodeExternalPointer(const Isolate* isolate,
                                        ExternalPointer_t encoded_pointer) {
  STATIC_ASSERT(kExternalPointerSize == kSystemPointerSize);
#ifdef V8_HEAP_SANDBOX
  if (V8_HEAP_SANDBOX_BOOL) {
    return isolate->external_pointer_table().get(encoded_pointer);
  }
#endif
  return encoded_pointer;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_EXTERNAL_POINTER_INL_H_
