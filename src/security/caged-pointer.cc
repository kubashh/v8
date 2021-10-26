// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/security/caged-pointer.h"

namespace v8 {
namespace internal {

#ifdef V8_CAGED_POINTERS

// Initialized by XXX
Address cagedPointerNullptrValue = 0;

#endif  // V8_CAGED_POINTERS

}  // namespace internal
}  // namespace v8
