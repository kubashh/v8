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
#ifdef V8_SANDBOXED_POINTERS
  SandboxedPointer_t sandboxed_pointer =
      base::ReadUnalignedValue<SandboxedPointer_t>(field_address);

  Address offset = sandboxed_pointer >> kSandboxedPointerShift;
  Address pointer = cage_base.address() + offset;
  return pointer;
#else
#if defined(V8_PROTECTED_FIELDS) && defined(V8_HOST_ARCH_ARM64)
  field_address |= (uint64_t{0xa} << kJSAsanTagShift);
#endif
  return ReadMaybeUnalignedValue<Address>(field_address);
#endif
}

V8_INLINE void WriteSandboxedPointerField(Address field_address,
                                          PtrComprCageBase cage_base,
                                          Address pointer) {
#ifdef V8_SANDBOXED_POINTERS
  // The pointer must point into the sandbox.
  CHECK(GetProcessWideSandbox()->Contains(pointer));

  Address offset = pointer - cage_base.address();
  SandboxedPointer_t sandboxed_pointer = offset << kSandboxedPointerShift;
  base::WriteUnalignedValue<SandboxedPointer_t>(field_address,
                                                sandboxed_pointer);
#else
#ifdef V8_PROTECTED_FIELDS
  // TODO(pierre.langlois@arm.com): We should have a seperate initialization
  // function for sandboxed pointers, like for external pointers.
  // DCHECK_IMPLIES(FLAG_protected_object_fields,
  //                VirtualMemoryCage::ReadJSAsanTag(field_address) == 0x0);
  // DCHECK_IMPLIES(
  //     FLAG_protected_object_fields,
  //     VirtualMemoryCage::ReadJSAsanTag(field_address + kTaggedSize) == 0x0);
  Heap::InitializeJSAsanProtectedField(MemoryChunk::FromAddress(field_address),
                                       field_address);
#if defined(V8_HOST_ARCH_ARM64)
  field_address |= (uint64_t{0xa} << kJSAsanTagShift);
#endif
#endif
  WriteMaybeUnalignedValue<Address>(field_address, pointer);
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_SANDBOXED_POINTER_INL_H_
