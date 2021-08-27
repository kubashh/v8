// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_CAGED_POINTER_H_
#define V8_COMMON_CAGED_POINTER_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

#ifdef V8_VIRTUAL_MEMORY_CAGE

// Nullptr is not allowed as caged pointer.
// The reason is that UncagePointer(CagePointer(nullptr)) == cage_base, and so
// nullptr would be indistinguishable from a pointer to the cage base. This
// could in turn lead to security issues, as what would otherwise be a nullptr
// dereference would not corrupt memory inside the V8 heap. Moreover, XXX
// Clients that need to store a XXX value can use this constant instead, which
// would, when incorrectly dereferenced, access data at the end of the cage and
// thus most likely touch a guard page.
constexpr Address kCagedPointerNullptrValue = static_cast<Address>(-1);

// TODO(saelo)
V8_INLINE Address ReadCagedPointerField(Address field_address,
                                        PtrComprCageBase cage_base);

// TODO(saelo)
V8_INLINE void WriteCagedPointerField(Address field_address,
                                      PtrComprCageBase cage_base,
                                      Address value);

// TODO(saelo)
V8_INLINE Address ReadCagedPointerFieldAllowNullptr(Address field_address,
                                                    PtrComprCageBase cage_base);

// TODO(saelo)
V8_INLINE void WriteCagedPointerFieldAllowNullptr(Address field_address,
                                                  PtrComprCageBase cage_base,
                                                  Address value);

#endif  // V8_VIRTUAL_MEMORY_CAGE

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_CAGED_POINTER_H_
