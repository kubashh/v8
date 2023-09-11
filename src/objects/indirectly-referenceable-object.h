// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_INDIRECTLY_REFERENCEABLE_OBJECT_H_
#define V8_OBJECTS_INDIRECTLY_REFERENCEABLE_OBJECT_H_

#include "src/objects/heap-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/indirectly-referenceable-object-tq.inc"

// An object that can be referenced through an indirect pointer.
//
// When the sandbox is enabled, some (internal) objects are allocated outside
// of the sandbox (in trusted space) where they cannot be corrupted by an
// attacker. These objects must then be referenced from inside the sandbox
// using an "indirect pointer": an index into a pointer table that contains the
// "real" pointer. This mechanism ensures memory-safe access.
// We want to have one such table entry per referenced object, *not* per
// reference. As such, there must be a way to obtain an existing table entry
// from a given (indirectly-referenceable) object. This base class provides
// that table entry.
//
// Indirectly-referenceable objects are always trusted objects (in the sense
// that they live in trusted space), but not all trusted objects are indirectly
// referenceable since there can be objects in trusted space that are only
// (directly) referenced from other trusted objects, and so do not need to be
// indirectly referenceable.
class IndirectlyReferenceableObject
    : public TorqueGeneratedIndirectlyReferenceableObject<
          IndirectlyReferenceableObject, HeapObject> {
 public:
  DECL_VERIFIER(IndirectlyReferenceableObject)

 protected:
  TQ_OBJECT_CONSTRUCTORS(IndirectlyReferenceableObject)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_INDIRECTLY_REFERENCEABLE_OBJECT_H_
