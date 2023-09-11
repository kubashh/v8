// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TRUSTED_BYTE_ARRAY_INL_H_
#define V8_OBJECTS_TRUSTED_BYTE_ARRAY_INL_H_

#include "src/objects/objects-inl.h"
#include "src/objects/trusted-byte-array.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/trusted-byte-array-tq-inl.inc"

RELEASE_ACQUIRE_SMI_ACCESSORS(TrustedByteArray, length, kLengthOffset)

CAST_ACCESSOR(TrustedByteArray)
TQ_OBJECT_CONSTRUCTORS_IMPL(TrustedByteArray)

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_TRUSTED_BYTE_ARRAY_INL_H_
