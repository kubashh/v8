// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_SANDBOXED_POINTER_INL_H_
#define V8_SANDBOX_SANDBOXED_POINTER_INL_H_

#include "include/v8-internal.h"
#include "src/common/ptr-compr.h"
#include "src/execution/isolate.h"
#include "src/sandbox/sandboxed-pointer.h"

namespace v8 {
namespace internal {

V8_INLINE Address ReadSandboxedPointerField(Address field_address,
                                            PtrComprCageBase cage_base) {
  return ReadMaybeUnalignedValue<Address>(field_address);
}

V8_INLINE void WriteSandboxedPointerField(Address field_address,
                                          PtrComprCageBase cage_base,
                                          Address pointer) {
  WriteMaybeUnalignedValue<Address>(field_address, pointer);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_SANDBOXED_POINTER_INL_H_
