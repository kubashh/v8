// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_NUMBER_FORMAT_H_
#define V8_OBJECTS_JS_NUMBER_FORMAT_H_

#include "src/heap/factory.h"
#include "src/isolate.h"
#include "src/objects.h"
#include "src/objects/intl-objects.h"
#include "src/objects/managed.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class JSNumberFormat : public JSObject {
 public:
  // ecma402/#sec-initializenumberformat
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSNumberFormat>
  InitializeNumberFormat(Isolate* isolate, Handle<JSNumberFormat> collator,
                         Handle<Object> locales, Handle<Object> options);

  // ecma402/#sec-intl.numberformat.prototype.resolvedoptions
  static Handle<JSObject> ResolvedOptions(Isolate* isolate,
                                          Handle<JSNumberFormat> collator);

  DECL_CAST(JSNumberFormat)
  DECL_PRINTER(JSNumberFormat)
  DECL_VERIFIER(JSNumberFormat)

// [[Usage]] is one of the values "sort" or "search", identifying
// the collator usage.
/*
enum class Usage {
  SORT,
  SEARCH,

  COUNT
};
inline void set_usage(Usage usage);
inline Usage usage() const;
static const char* UsageToString(Usage usage);
*/

// Layout description.
#define JS_NUMBER_FORMAT_FIELDS(V)        \
  V(kIcuNumberFormatOffset, kPointerSize) \
  V(kFlagsOffset, kPointerSize)           \
  /* Total size. */                       \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(JSObject::kHeaderSize, JS_NUMBER_FORMAT_FIELDS)
#undef JS_NUMBER_FORMAT_FIELDS

  // Bit positions in |flags|.
  //#define FLAGS_BIT_FIELDS(V, _) V(UsageBits, Usage, 1, _)

  //  DEFINE_BIT_FIELDS(FLAGS_BIT_FIELDS)
  //#undef FLAGS_BIT_FIELDS

  //  STATIC_ASSERT(Usage::SORT <= UsageBits::kMax);
  //  STATIC_ASSERT(Usage::SEARCH <= UsageBits::kMax);

  DECL_ACCESSORS(icu_number_format, Managed<icu::NumberFormat>)
  DECL_INT_ACCESSORS(flags)

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSNumberFormat);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_NUMBER_FORMAT_H_
