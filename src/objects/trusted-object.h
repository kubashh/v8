// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TRUSTED_OBJECT_H_
#define V8_OBJECTS_TRUSTED_OBJECT_H_

#include "src/objects/heap-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/trusted-object-tq.inc"

// An object that is trusted to not have been modified in a malicious way.
//
// Typical examples of trusted objects are containers for bytecode or code
// metadata, which often allow an attacker to corrupt (for example) stack
// memory when manipulated. When the sandbox is enabled, trusted objects are
// located outside of the sandbox (in one of the trusted heap spaces) so that
// attackers cannot corrupt these objects and use them to escape from the
// sandbox. When the sandbox is disabled, trusted objects are treated like any
// other objects since in that case, many other types of objects (for example
// ArrayBuffers) can be used to corrupt memory outside of V8's heaps as well.
//
// Trusted objects cannot directly be referenced from untrusted objects as this
// would be unsafe: an attacker could corrupt any (direct) pointer to these
// objects stored inside the sandbox. However, ExposedTrustedObject can be
// referenced via indirect pointers, which guarantee memory-safe access.
class TrustedObject
    : public TorqueGeneratedTrustedObject<TrustedObject, HeapObject> {
 public:
  DECL_VERIFIER(TrustedObject)

 protected:
  TQ_OBJECT_CONSTRUCTORS(TrustedObject)
};

// A trusted object that can safely be referenced from untrusted objects.
//
// These objects live in trusted space but are "exposed" to untrusted objects
// living inside the sandbox. They still cannot be referenced through "direct"
// pointers (these can be corrupted by an attacker), but instead they must be
// referenced through "indirect pointers": an index into a pointer table that
// contains the actual pointer as well as a type tag. This mechanism then
// guarantees memory-safe access.
//
// We want to have one pointer table entry per referenced object, *not* per
// reference. As such, there must be a way to obtain an existing table entry
// from a given object. This base class provides that table entry.
class ExposedTrustedObject
    : public TorqueGeneratedExposedTrustedObject<ExposedTrustedObject,
                                                 TrustedObject> {
 public:
  DECL_VERIFIER(ExposedTrustedObject)

 protected:
  TQ_OBJECT_CONSTRUCTORS(ExposedTrustedObject)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_TRUSTED_OBJECT_H_
