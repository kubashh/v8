// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_DURATION_FORMAT_H_
#define V8_OBJECTS_JS_DURATION_FORMAT_H_

#include <set>
#include <string>

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/managed.h"
#include "src/objects/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {
class DurationFormatInternal;

#include "torque-generated/src/objects/js-duration-format-tq.inc"

class JSDurationFormat
    : public TorqueGeneratedJSDurationFormat<JSDurationFormat, JSObject> {
 public:
  // Creates display names object with properties derived from input
  // locales and options.
  static MaybeHandle<JSDurationFormat> New(Isolate* isolate, Handle<Map> map,
                                           Handle<Object> locales,
                                           Handle<Object> options);

  static Handle<JSObject> ResolvedOptions(
      Isolate* isolate, Handle<JSDurationFormat> format_holder);

  V8_WARN_UNUSED_RESULT static MaybeHandle<String> Format(
      Isolate* isolate, Handle<Object> value_obj,
      Handle<JSDurationFormat> format);

  V8_WARN_UNUSED_RESULT static MaybeHandle<JSArray> FormatToParts(
      Isolate* isolate, Handle<Object> value_obj,
      Handle<JSDurationFormat> format);

  V8_EXPORT_PRIVATE static const std::set<std::string>& GetAvailableLocales();

  DECL_ACCESSORS(internal, Managed<DurationFormatInternal>)

  DECL_PRINTER(JSDurationFormat)
  TQ_OBJECT_CONSTRUCTORS(JSDurationFormat)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DURATION_FORMAT_H_
