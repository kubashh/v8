// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_ASYNC_CONTEXT_H_
#define V8_OBJECTS_JS_ASYNC_CONTEXT_H_

#include "src/objects/js-objects.h"
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-async-context-tq.inc"

// AsyncContext.Variable object from the JS AsyncContext spec proposal:
// https://github.com/tc39/proposal-async-context
class JSAsyncContextVariable
    : public TorqueGeneratedJSAsyncContextVariable<JSAsyncContextVariable,
                                                   JSObject> {
 public:
  DECL_PRINTER(JSAsyncContextVariable)
  EXPORT_DECL_VERIFIER(JSAsyncContextVariable)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(JSAsyncContextVariable)
};

class JSAsyncContextSnapshot
    : public TorqueGeneratedJSAsyncContextSnapshot<JSAsyncContextSnapshot,
                                                   JSObject> {
 public:
  DECL_PRINTER(JSAsyncContextSnapshot)
  EXPORT_DECL_VERIFIER(JSAsyncContextSnapshot)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(JSAsyncContextSnapshot)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_ASYNC_CONTEXT_H_
