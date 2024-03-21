// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/external-pointer-table.h"

#include "src/execution/isolate.h"
#include "src/heap/read-only-spaces.h"
#include "src/logging/counters.h"
#include "src/sandbox/external-pointer-table-inl.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

void ExternalPointerTable::SetUpFromReadOnlyArtifacts(
    Space* read_only_space, const ReadOnlyArtifacts* artifacts) {
  UnsealReadOnlySegmentScope unseal_scope(this);
  for (const auto& registry_entry : artifacts->external_pointer_registry()) {
    ExternalPointerHandle handle = AllocateAndInitializeEntry(
        read_only_space, registry_entry.value, registry_entry.tag);
    CHECK_EQ(handle, registry_entry.handle);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS
