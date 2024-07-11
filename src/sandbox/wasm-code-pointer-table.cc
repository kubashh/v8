// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/wasm-code-pointer-table.h"

#include "src/sandbox/external-entity-table-inl.h"
#include "src/sandbox/wasm-code-pointer-table-inl.h"

#ifdef V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

void WasmCodePointerTable::Initialize() {
  ExternalEntityTable<WasmCodePointerTableEntry,
                      kCodePointerTableReservationSize>::Initialize();
  CHECK(ThreadIsolation::WriteProtectMemory(
      base(), kCodePointerTableReservationSize,
      PageAllocator::Permission::kNoAccess));

  InitializeSpace(&space_);
}

DEFINE_LAZY_LEAKY_OBJECT_GETTER(WasmCodePointerTable,
                                GetProcessWideWasmCodePointerTable)

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_WEBASSEMBLY
