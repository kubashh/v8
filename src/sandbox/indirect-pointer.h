// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_INDIRECT_POINTER_H_
#define V8_SANDBOX_INDIRECT_POINTER_H_

#include "src/common/globals.h"
#include "src/sandbox/indirect-pointer-tag.h"

namespace v8 {
namespace internal {

// Reads the IndirectPointerHandle from the field and loads the Object
// referenced by this handle from the pointer table. The given
// IndirectPointerTag specifies the expected type of object.
//
// Only available when the sandbox is enabled.
template <IndirectPointerTag tag>
V8_INLINE Object ReadIndirectPointerField(Address field_address,
                                          const Isolate* isolate);

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_INDIRECT_POINTER_H_
