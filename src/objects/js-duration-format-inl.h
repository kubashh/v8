// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_DURATION_FORMAT_INL_H_
#define V8_OBJECTS_JS_DURATION_FORMAT_INL_H_

#include "src/objects/js-duration-format.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-duration-format-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSDurationFormat)

ACCESSORS(JSDurationFormat, internal,
          Managed<v8::internal::DurationFormatInternal>, kInternalOffset)

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DURATION_FORMAT_INL_H_
