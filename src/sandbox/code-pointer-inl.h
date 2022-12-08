// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_CODE_POINTER_INL_H_
#define V8_SANDBOX_CODE_POINTER_INL_H_

#include "include/v8-internal.h"
#include "src/base/atomic-utils.h"
#include "src/execution/isolate.h"
#include "src/sandbox/code-pointer-table.h"
#include "src/sandbox/code-pointer.h"
#include "src/sandbox/external-entity-table-inl.h"

namespace v8 {
namespace internal {

V8_INLINE void InitCodePointerField(Address field_address, Isolate* isolate,
                                    Address value) {
#ifdef V8_ENABLE_SANDBOX
  CodePointerTable& table = isolate->code_pointer_table();
  ExternalPointerHandle handle =
      table.AllocateAndInitializeEntry(isolate, value, kExternalPointerNullTag);
  // Use a Release_Store to ensure that the store of the pointer into the
  // table is not reordered after the store of the handle. Otherwise, other
  // threads may access an uninitialized table entry and crash.
  auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
  base::AsAtomic32::Release_Store(location, handle);
#else
  WriteCodePointerField(field_address, isolate, value);
#endif  // V8_ENABLE_SANDBOX
}

V8_INLINE Address ReadCodePointerField(Address field_address,
                                       const Isolate* isolate) {
#ifdef V8_ENABLE_SANDBOX
  // Handles may be written to objects from other threads so the handle needs
  // to be loaded atomically. We assume that the load from the table cannot
  // be reordered before the load of the handle due to the data dependency
  // between the two loads and therefore use relaxed memory ordering.
  auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
  ExternalPointerHandle handle = base::AsAtomic32::Relaxed_Load(location);
  return isolate->code_pointer_table().Get(handle, kExternalPointerNullTag);
#else
  return ReadMaybeUnalignedValue<Address>(field_address);
#endif  // V8_ENABLE_SANDBOX
}

V8_INLINE void WriteCodePointerField(Address field_address, Isolate* isolate,
                                     Address value) {
#ifdef V8_ENABLE_SANDBOX
  // See comment above for why this is a Relaxed_Load.
  auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
  ExternalPointerHandle handle = base::AsAtomic32::Relaxed_Load(location);
  isolate->code_pointer_table().Set(handle, value, kExternalPointerNullTag);
#else
  WriteMaybeUnalignedValue<Address>(field_address, value);
#endif  // V8_ENABLE_SANDBOX
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_CODE_POINTER_INL_H_
