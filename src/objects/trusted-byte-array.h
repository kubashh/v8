// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TRUSTED_BYTE_ARRAY_H_
#define V8_OBJECTS_TRUSTED_BYTE_ARRAY_H_

#include "src/objects/trusted-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/trusted-byte-array-tq.inc"

// A byte array containing trusted data.
//
// These can be used to securely store data such as interpreter bytecode or JIT
// metadata (e.g. deoptimization data), which must not be corrupted by an
// attacker, when the sandbox is enabled.
class TrustedByteArray
    : public TorqueGeneratedTrustedByteArray<TrustedByteArray,
                                             ExposedTrustedObject> {
 public:
  DECL_RELEASE_ACQUIRE_INT_ACCESSORS(length)

  DECL_CAST(TrustedByteArray)
  DECL_VERIFIER(TrustedByteArray)

  class BodyDescriptor;

 protected:
  TQ_OBJECT_CONSTRUCTORS(TrustedByteArray)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_TRUSTED_BYTE_ARRAY_H_
