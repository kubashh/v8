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

// AsyncContext object from the JS AsyncContext spec proposal:
// https://github.com/tc39/proposal-async-context
class JSAsyncLocal
    : public TorqueGeneratedJSAsyncLocal<JSAsyncLocal, JSObject> {
 public:
  DECL_PRINTER(JSAsyncLocal)
  EXPORT_DECL_VERIFIER(JSAsyncLocal)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(JSAsyncLocal)
};

class JSAsyncSnapshot
    : public TorqueGeneratedJSAsyncSnapshot<JSAsyncSnapshot, JSObject> {
 public:
  DECL_PRINTER(JSAsyncSnapshot)
  EXPORT_DECL_VERIFIER(JSAsyncSnapshot)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(JSAsyncSnapshot)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_ASYNC_CONTEXT_H_
