// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SECURITY_CAGED_POINTER_H_
#define V8_SECURITY_CAGED_POINTER_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

#ifdef V8_CAGED_POINTERS

// A CagedPointer cannot generally represent nullptr, as kNullAddress usually
// lies outside the cage.
//
// While XXX
// The reason is that UncagePointer(CagePointer(nullptr)) == cage_base, and so
// nullptr would be indistinguishable from a pointer to the cage base. This
// could in turn lead to security issues, as what would otherwise be a nullptr
// dereference would not corrupt memory inside the V8 heap. Moreover, XXX
// Clients that need to store a XXX value can use this constant instead, which
// would, when incorrectly dereferenced, access data at the end of the cage and
// thus most likely touch a guard page.
// TODO(saelo) name this kCagedPointerNullptrValueOffset or so?
constexpr Address kCagedPointerNullptrValueOffset =
    static_cast<Address>(-1) & (kVirtualMemoryCageSize - 1);
extern Address cagedPointerNullptrValue;

V8_INLINE CagedPointer_t ReadCagedPointerField(Address field_address,
                                               PtrComprCageBase cage_base);

V8_INLINE void WriteCagedPointerField(Address field_address,
                                      PtrComprCageBase cage_base,
                                      CagedPointer_t value);

#endif  // V8_CAGED_POINTERS

}  // namespace internal
}  // namespace v8

#endif  // V8_SECURITY_CAGED_POINTER_H_
