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

namespace U_ICU_NAMESPACE {
namespace number {
class LocalizedNumberFormatter;
}  // namespace number
}  // namespace U_ICU_NAMESPACE

namespace v8 {
namespace internal {

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

  // icu::number::LocalizedNumberFormatter accessors.
  DECL_ACCESSORS(icu_number_formatter,
                 Managed<icu::number::LocalizedNumberFormatter>)

  Handle<String> StyleAsString() const;

  enum Field {
    kYears,
    kMonths,
    kWeeks,
    kDays,
    kHours,
    kMinutes,
    kSeconds,
    kMilliseconds,
    kMicroseconds,
    kNanoseconds,

    kCount
  };
  inline void set_largest_unit(Field largest_unit);
  inline Field largest_unit() const;
  inline void set_smallest_unit(Field smallest_unit);
  inline Field smallest_unit() const;

  // Bit positions in |flags|.
  DEFINE_TORQUE_GENERATED_JS_DURATION_FORMAT_FLAGS()

  STATIC_ASSERT(Field::kYears <= LargestUnitBits::kMax);
  STATIC_ASSERT(Field::kMonths <= LargestUnitBits::kMax);
  STATIC_ASSERT(Field::kWeeks <= LargestUnitBits::kMax);
  STATIC_ASSERT(Field::kDays <= LargestUnitBits::kMax);
  STATIC_ASSERT(Field::kHours <= LargestUnitBits::kMax);
  STATIC_ASSERT(Field::kMinutes <= LargestUnitBits::kMax);
  STATIC_ASSERT(Field::kSeconds <= LargestUnitBits::kMax);
  STATIC_ASSERT(Field::kMilliseconds <= LargestUnitBits::kMax);
  STATIC_ASSERT(Field::kMicroseconds <= LargestUnitBits::kMax);
  STATIC_ASSERT(Field::kNanoseconds <= LargestUnitBits::kMax);

  STATIC_ASSERT(Field::kYears <= SmallestUnitBits::kMax);
  STATIC_ASSERT(Field::kMonths <= SmallestUnitBits::kMax);
  STATIC_ASSERT(Field::kWeeks <= SmallestUnitBits::kMax);
  STATIC_ASSERT(Field::kDays <= SmallestUnitBits::kMax);
  STATIC_ASSERT(Field::kHours <= SmallestUnitBits::kMax);
  STATIC_ASSERT(Field::kMinutes <= SmallestUnitBits::kMax);
  STATIC_ASSERT(Field::kSeconds <= SmallestUnitBits::kMax);
  STATIC_ASSERT(Field::kMilliseconds <= SmallestUnitBits::kMax);
  STATIC_ASSERT(Field::kMicroseconds <= SmallestUnitBits::kMax);
  STATIC_ASSERT(Field::kNanoseconds <= SmallestUnitBits::kMax);

  DECL_PRINTER(JSDurationFormat)
  TQ_OBJECT_CONSTRUCTORS(JSDurationFormat)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DURATION_FORMAT_H_
