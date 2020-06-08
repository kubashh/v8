// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_EXTERNAL_POINTER_H_
#define V8_COMMON_EXTERNAL_POINTER_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

// Convert external pointer from on-V8-heap representation to an actual external
// pointer value.
V8_INLINE Address DecodeExternalPointer(const Isolate* isolate,
                                        ExternalPointer_t encoded_pointer);

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_EXTERNAL_POINTER_H_
