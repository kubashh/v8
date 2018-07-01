// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_RELATIVE_TIME_FORMAT_H_
#define V8_OBJECTS_JS_RELATIVE_TIME_FORMAT_H_

#include "src/api.h"
#include "src/global-handles.h"
#include "src/heap/factory.h"
#include "src/isolate.h"
#include "src/objects.h"
#include "unicode/unistr.h"
#include "unicode/uversion.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace U_ICU_NAMESPACE {
class RelativeDateTimeFormatter;
}

namespace v8 {
namespace internal {

class JSRelativeTimeFormat : public JSObject {
 public:
  // Initializes relative time format object with properties derived from input
  // locales and options.
  static MaybeHandle<JSRelativeTimeFormat> InitializeRelativeTimeFormat(
      Isolate* isolate,
      Handle<JSRelativeTimeFormat> relative_time_format_holder,
      Handle<Object> locales, Handle<Object> options);

  static Object* ResolvedOptions(Isolate* isolate,
                                 Handle<JSRelativeTimeFormat> format_holder);

  // Unpacks formatter object from corresponding JavaScript object.
  static icu::RelativeDateTimeFormatter* UnpackFormatter(
      Isolate* isolate,
      Handle<JSRelativeTimeFormat> relative_time_format_holder);
  Handle<String> StyleAsString(Isolate* isolate) const;
  Handle<String> NumericAsString(Isolate* isolate) const;

  DECL_CAST(JSRelativeTimeFormat)

  // RelativeTimeFormat accessors.
  DECL_ACCESSORS(locale, String)
  DECL_INT_ACCESSORS(style)
  enum Style { STYLE_LONG, STYLE_SHORT, STYLE_NARROW };
  DECL_INT_ACCESSORS(numeric)
  enum Numeric { NUMERIC_ALWAYS, NUMERIC_AUTO };
  DECL_ACCESSORS(formatter, Object)

  DECL_PRINTER_WITH_ISOLATE(JSRelativeTimeFormat)
  DECL_VERIFIER(JSRelativeTimeFormat)

  // Layout description.
  static const int kJSRelativeTimeFormatOffset = JSObject::kHeaderSize;
  // Locale fields.
  static const int kLocaleOffset = kJSRelativeTimeFormatOffset + kPointerSize;
  static const int kStyleOffset = kLocaleOffset + kPointerSize;
  static const int kNumericOffset = kStyleOffset + kPointerSize;
  static const int kFormatterOffset = kNumericOffset + kPointerSize;
  // Final size.
  static const int kSize = kFormatterOffset + kPointerSize;

  // Constant to access field
  static const int kFormatterField = 3;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSRelativeTimeFormat);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_RELATIVE_TIME_FORMAT_H_
