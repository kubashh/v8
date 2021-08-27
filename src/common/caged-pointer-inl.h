// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_CAGED_POINTER_INL_H_
#define V8_COMMON_CAGED_POINTER_INL_H_

#include "include/v8-internal.h"
#include "src/common/caged-pointer.h"
#include "src/execution/isolate.h"

namespace v8 {
namespace internal {

#ifdef V8_VIRTUAL_MEMORY_CAGE

V8_INLINE Address ReadCagedPointerField(Address field_address,
                                        PtrComprCageBase cage_base) {
  // Caged pointers are currently only used if the sandbox is enabled.
  DCHECK(V8_HEAP_SANDBOX_BOOL);

  Address caged_pointer = base::ReadUnalignedValue<Address>(field_address);

  // nullptr is forbidden.
  DCHECK(caged_pointer);

  Address offset = caged_pointer >> kCagedPointerShift;
  Address pointer = cage_base.address() + offset;
  return pointer;
}

V8_INLINE void WriteCagedPointerField(Address field_address,
                                      PtrComprCageBase cage_base,
                                      Address pointer) {
  // Caged pointers are currently only used if the sandbox is enabled.
  DCHECK(V8_HEAP_SANDBOX_BOOL);

  // nullptr is forbidden.
  CHECK(pointer);
  // The pointer must point into the virtual memory cage.
  DCHECK(GetProcessWideVirtualMemoryCage()->Contains(pointer));

  Address offset = pointer - cage_base.address();
  Address caged_pointer = offset << kCagedPointerShift;
  base::WriteUnalignedValue<Address>(field_address, caged_pointer);
}

V8_INLINE Address ReadCagedPointerFieldAllowNullptr(
    Address field_address, PtrComprCageBase cage_base) {
  // Caged pointers are currently only used if the sandbox is enabled.
  DCHECK(V8_HEAP_SANDBOX_BOOL);

  Address caged_pointer = base::ReadUnalignedValue<Address>(field_address);

  // Actual nullptr is still forbidden.
  DCHECK(caged_pointer);

  if (caged_pointer == kCagedPointerNullptrValue) {
    return kNullAddress;
  }

  Address offset = caged_pointer >> kCagedPointerShift;
  Address pointer = cage_base.address() + offset;
  return pointer;
}

V8_INLINE void WriteCagedPointerFieldAllowNullptr(Address field_address,
                                                  PtrComprCageBase cage_base,
                                                  Address pointer) {
  if (pointer == kNullAddress) {
    base::WriteUnalignedValue<Address>(field_address,
                                       kCagedPointerNullptrValue);
    return;
  }

  WriteCagedPointerField(field_address, cage_base, pointer);
}

#endif  // V8_VIRTUAL_MEMORY_CAGE

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_CAGED_POINTER_INL_H_
