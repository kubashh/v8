// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/property-cell.h"

#include "src/objects/property-cell-inl.h"

namespace v8 {
namespace internal {

// static
void PropertyCell::Init(Isolate* isolate, PropertyCell cell,
                        DisallowGarbageCollection&, Handle<Name> name,
                        PropertyDetails details, Handle<Object> value,
                        WriteBarrierMode write_barrier_mode) {
  DCHECK(name->IsUniqueName());

  cell.set_dependent_code(
      DependentCode::empty_dependent_code(ReadOnlyRoots(isolate)),
      SKIP_WRITE_BARRIER);
  cell.set_name(*name, write_barrier_mode);
  cell.set_value(*value, write_barrier_mode);
  cell.set_property_details_raw(details.AsSmi(), SKIP_WRITE_BARRIER);
}

}  // namespace internal
}  // namespace v8
