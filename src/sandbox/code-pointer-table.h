// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_CODE_POINTER_TABLE_H_
#define V8_SANDBOX_CODE_POINTER_TABLE_H_

#include "src/sandbox/external-entity-table.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8 {
namespace internal {

// A code pointer table contains pointers to native code functions.
using CodePointerTable = ExternalEntityTable<kCodePointerMarkBit>;

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX

#endif  // V8_SANDBOX_CODE_POINTER_TABLE_H_
