// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-duration-format.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-duration-format-inl.h"
#include "src/objects/js-number-format.h"
#include "src/objects/managed.h"
#include "src/objects/objects-inl.h"
#include "unicode/decimfmt.h"
#include "unicode/measunit.h"
#include "unicode/numberformatter.h"
#include "unicode/udata.h"
#include "unicode/unumberformatter.h"

namespace v8 {
namespace internal {

namespace {

enum class Style {
  kLong,
  kShort,
  kNarrow,
  kDotted,
};

UNumberUnitWidth toWidth(Style style) {
  switch (style) {
    case Style::kLong:
      return UNUM_UNIT_WIDTH_FULL_NAME;
    case Style::kShort:
      return UNUM_UNIT_WIDTH_SHORT;
    case Style::kNarrow:
      return UNUM_UNIT_WIDTH_NARROW;
    case Style::kDotted:
      return UNUM_UNIT_WIDTH_HIDDEN;
  }
  UNREACHABLE();
}

}  // anonymous namespace

Maybe<std::string> FieldsToUnits(Isolate* isolate, Handle<Object> fields,
                                 JSDurationFormat::Field* smallest_unit,
                                 JSDurationFormat::Field* largest_unit) {
  bool fields_bool[JSDurationFormat::Field::kCount];
  // If fields is undefined, then
  if (fields->IsUndefined(isolate)) {
    for (int i = 0; i < JSDurationFormat::Field::kCount; i++) {
      fields_bool[i] = true;
    }
  } else {
    // Else if IsArray(fields) is true, then
    Maybe<bool> is_array = Object::IsArray(fields);
    MAYBE_RETURN(is_array, Nothing<std::string>());
    if (is_array.FromJust()) {
      // Set fields to ? CreateListFromArrayLike(fields).
      Handle<FixedArray> fields_list;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, fields_list,
          Object::CreateListFromArrayLike(isolate, fields, ElementTypes::kAll),
          Nothing<std::string>());
      for (int i = 0; i < JSDurationFormat::Field::kCount; i++) {
        fields_bool[i] = false;
      }
      std::map<std::string, JSDurationFormat::Field> kProperties = {
          {"years", JSDurationFormat::Field::kYears},
          {"months", JSDurationFormat::Field::kMonths},
          {"weeks", JSDurationFormat::Field::kWeeks},
          {"days", JSDurationFormat::Field::kDays},
          {"hours", JSDurationFormat::Field::kHours},
          {"minutes", JSDurationFormat::Field::kMinutes},
          {"seconds", JSDurationFormat::Field::kSeconds},
          {"milliseconds", JSDurationFormat::Field::kMilliseconds},
          {"microseconds", JSDurationFormat::Field::kMicroseconds},
          {"nanoseconds", JSDurationFormat::Field::kNanoseconds}};
      // For each field in fields, do
      for (int i = 0; i < fields_list->length(); i++) {
        Handle<Object> field_obj = FixedArray::get(*fields_list, i, isolate);
        if (field_obj->IsString()) {
          std::string field_str(
              Handle<String>::cast(field_obj)->ToCString().get());
          if (kProperties.find(field_str) != kProperties.end()) {
            fields_bool[kProperties.find(field_str)->second] = true;
            continue;
          }
        }
        return Nothing<std::string>();
      }
    } else {  // Else
      return Nothing<std::string>();
    }
  }
  std::string units;
  std::vector<std::string> unit_str({"year", "month", "week", "day", "hour",
                                     "minute", "second", "millisecond",
                                     "microsecond", "nanosecond"});
  for (size_t i = 0; i < unit_str.size(); i++) {
    units +=
        fields_bool[i] ? ((units.empty() ? "" : "-and-") + unit_str[i]) : "";
  }
  *largest_unit = JSDurationFormat::Field::kNanoseconds;
  for (int i = JSDurationFormat::Field::kYears;
       i < JSDurationFormat::Field::kCount; i++) {
    if (fields_bool[i]) {
      *largest_unit = static_cast<JSDurationFormat::Field>(i);
      break;
    }
  }
  *smallest_unit = JSDurationFormat::Field::kYears;
  for (int i = JSDurationFormat::Field::kNanoseconds;
       i >= JSDurationFormat::Field::kYears; i--) {
    if (fields_bool[i]) {
      *smallest_unit = static_cast<JSDurationFormat::Field>(i);
      break;
    }
  }
  return Just(units);
}

MaybeHandle<JSDurationFormat> JSDurationFormat::New(
    Isolate* isolate, Handle<Map> map, Handle<Object> locales,
    Handle<Object> input_options) {
  const char* service = "Intl.DurationFormat";
  Factory* factory = isolate->factory();

  Handle<JSReceiver> options;
  Maybe<std::vector<std::string>> maybe_requested_locales =
      Intl::CanonicalizeLocaleList(isolate, locales);
  MAYBE_RETURN(maybe_requested_locales, Handle<JSDurationFormat>());
  std::vector<std::string> requested_locales =
      maybe_requested_locales.FromJust();

  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, Intl::GetOptionsObject(isolate, input_options, service),
      JSDurationFormat);

  // Let matcher be ? GetOption(options, "localeMatcher", "string", «
  // "lookup", "best fit" », "best fit").
  Maybe<Intl::MatcherOption> maybe_locale_matcher =
      Intl::GetLocaleMatcher(isolate, options, service);
  MAYBE_RETURN(maybe_locale_matcher, MaybeHandle<JSDurationFormat>());
  // Set opt.[[localeMatcher]] to matcher.
  Intl::MatcherOption matcher = maybe_locale_matcher.FromJust();

  // Let numberingSystem be ? GetOption(options, "numberingSystem",
  //    "string", undefined, undefined).
  std::unique_ptr<char[]> numbering_system_str = nullptr;
  Maybe<bool> maybe_numberingSystem = Intl::GetNumberingSystem(
      isolate, options, service, &numbering_system_str);
  MAYBE_RETURN(maybe_numberingSystem, MaybeHandle<JSDurationFormat>());

  // Let r be ResolveLocale(%DurationFormat%.[[AvailableLocales]],
  //     requestedLocales, opt, %DurationFormat%.[[RelevantExtensionKeys]]).
  Maybe<Intl::ResolvedLocale> maybe_resolve_locale =
      Intl::ResolveLocale(isolate, JSDurationFormat::GetAvailableLocales(),
                          requested_locales, matcher, {"nu"});
  if (maybe_resolve_locale.IsNothing()) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError),
                    JSDurationFormat);
  }
  Intl::ResolvedLocale r = maybe_resolve_locale.FromJust();

  icu::Locale icu_locale = r.icu_locale;

  UErrorCode status = U_ZERO_ERROR;
  if (numbering_system_str != nullptr) {
    auto nu_extension_it = r.extensions.find("nu");
    if (nu_extension_it != r.extensions.end() &&
        nu_extension_it->second != numbering_system_str.get()) {
      icu_locale.setUnicodeKeywordValue("nu", nullptr, status);
      DCHECK(U_SUCCESS(status));
    }
  }
  // Let locale be r.[[Locale]].
  Maybe<std::string> maybe_locale_str = Intl::ToLanguageTag(icu_locale);
  MAYBE_RETURN(maybe_locale_str, MaybeHandle<JSDurationFormat>());

  // Set durationFormat.[[Locale]] to locale.
  Handle<String> locale_str = isolate->factory()->NewStringFromAsciiChecked(
      maybe_locale_str.FromJust().c_str());

  // Set durationFormat.[[NumberingSystem]] to r.[[nu]].
  if (numbering_system_str != nullptr &&
      Intl::IsValidNumberingSystem(numbering_system_str.get())) {
    icu_locale.setUnicodeKeywordValue("nu", numbering_system_str.get(), status);
    DCHECK(U_SUCCESS(status));
  }
  // Let dataLocale be r.[[DataLocale]].
  std::string numbering_system = Intl::GetNumberingSystem(icu_locale);

  // 11. Let dataLocale be r.[[dataLocale]].

  icu::number::LocalizedNumberFormatter icu_number_formatter =
      icu::number::NumberFormatter::withLocale(icu_locale)
          .roundingMode(UNUM_ROUND_HALFUP);

  // For 'latn' numbering system, skip the adoptSymbols which would cause
  // 10.1%-13.7% of regression of JSTests/Intl-NewIntlNumberFormat
  // See crbug/1052751 so we skip calling adoptSymbols and depending on the
  // default instead.
  if (!numbering_system.empty() && numbering_system != "latn") {
    icu_number_formatter = icu_number_formatter.adoptSymbols(
        icu::NumberingSystem::createInstanceByName(numbering_system.c_str(),
                                                   status));
    CHECK(U_SUCCESS(status));
  }

  // Let s be ? GetOption(options, "style", "string",
  //                          «"long", "short", "narrow", "dotted"», "long").
  Maybe<Style> maybe_style = Intl::GetStringOption<Style>(
      isolate, options, "style", service, {"long", "short", "narrow", "dotted"},
      {Style::kLong, Style::kShort, Style::kNarrow, Style::kDotted},
      Style::kLong);
  MAYBE_RETURN(maybe_style, MaybeHandle<JSDurationFormat>());
  Style style_enum = maybe_style.FromJust();
  icu_number_formatter = icu_number_formatter.unitWidth(toWidth(style_enum));

  // Let fields be ? Get(options, "fields").
  Handle<Object> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      Object::GetPropertyOrElement(isolate, options, factory->fields_string()),
      JSDurationFormat);

  Field largest_unit;
  Field smallest_unit;
  std::string units =
      FieldsToUnits(isolate, fields, &smallest_unit, &largest_unit).FromJust();
  printf("FieldsToUnits returns %s %d\n", units.c_str(), largest_unit);

  icu_number_formatter = icu_number_formatter.unit(
      icu::MeasureUnit::forIdentifier(units.c_str(), status));

  Handle<Managed<icu::number::LocalizedNumberFormatter>>
      managed_number_formatter =
          Managed<icu::number::LocalizedNumberFormatter>::FromRawPtr(
              isolate, 0,
              new icu::number::LocalizedNumberFormatter(icu_number_formatter));

  Handle<JSDurationFormat> duration_format = Handle<JSDurationFormat>::cast(
      factory->NewFastOrSlowJSObjectFromMap(map));

  DisallowGarbageCollection no_gc;
  duration_format->set_flags(0);
  duration_format->set_locale(*locale_str);
  duration_format->set_icu_number_formatter(*managed_number_formatter);
  duration_format->set_smallest_unit(smallest_unit);
  duration_format->set_largest_unit(largest_unit);

  return duration_format;
}

namespace {

Handle<JSArray> GetFields(Isolate* isolate,
                          const icu::UnicodeString& skeleton) {
  Factory* factory = isolate->factory();
  Handle<JSArray> array = factory->NewJSArray(0);
  int len = 0;
  if (skeleton.indexOf("year") >= 0) {
    JSObject::AddDataElement(array, len++, factory->years_string(), NONE);
  }
  if (skeleton.indexOf("month") >= 0) {
    JSObject::AddDataElement(array, len++, factory->months_string(), NONE);
  }
  if (skeleton.indexOf("week") >= 0) {
    JSObject::AddDataElement(array, len++, factory->weeks_string(), NONE);
  }
  if (skeleton.indexOf("day") >= 0) {
    JSObject::AddDataElement(array, len++, factory->days_string(), NONE);
  }
  if (skeleton.indexOf("hour") >= 0) {
    JSObject::AddDataElement(array, len++, factory->hours_string(), NONE);
  }
  if (skeleton.indexOf("minute") >= 0) {
    JSObject::AddDataElement(array, len++, factory->minutes_string(), NONE);
  }
  if (skeleton.indexOf("second") >= 0) {
    JSObject::AddDataElement(array, len++, factory->seconds_string(), NONE);
  }
  if (skeleton.indexOf("millisecond") >= 0) {
    JSObject::AddDataElement(array, len++, factory->milliseconds_string(),
                             NONE);
  }
  if (skeleton.indexOf("microsecond") >= 0) {
    JSObject::AddDataElement(array, len++, factory->microseconds_string(),
                             NONE);
  }
  if (skeleton.indexOf("nanosecond") >= 0) {
    JSObject::AddDataElement(array, len++, factory->nanoseconds_string(), NONE);
  }
  JSObject::ValidateElements(*array);
  return array;
}

}  // namespace

Handle<JSObject> JSDurationFormat::ResolvedOptions(
    Isolate* isolate, Handle<JSDurationFormat> format) {
  Factory* factory = isolate->factory();
  UErrorCode status = U_ZERO_ERROR;
  icu::number::LocalizedNumberFormatter* icu_number_formatter =
      format->icu_number_formatter().raw();
  icu::UnicodeString skeleton = icu_number_formatter->toSkeleton(status);
  CHECK(U_SUCCESS(status));

  std::string utf8;
  printf("skeleton = %s\n", skeleton.toUTF8String<std::string>(utf8).c_str());

  Handle<JSObject> result = factory->NewJSObject(isolate->object_function());
  Handle<String> locale(format->locale(), isolate);
  const icu::UnicodeString numberingSystem_ustr =
      JSNumberFormat::NumberingSystemFromSkeleton(skeleton);
  Handle<String> numberingSystem_string;
  CHECK(Intl::ToString(isolate, numberingSystem_ustr)
            .ToHandle(&numberingSystem_string));
  Handle<JSArray> fields = GetFields(isolate, skeleton);
  JSObject::AddProperty(isolate, result, factory->locale_string(), locale,
                        NONE);
  JSObject::AddProperty(isolate, result, factory->numberingSystem_string(),
                        numberingSystem_string, NONE);
  JSObject::AddProperty(isolate, result, factory->style_string(),
                        format->StyleAsString(), NONE);
  JSObject::AddProperty(isolate, result, factory->fields_string(), fields,
                        NONE);

  return result;
}

namespace {

Maybe<int> GetNumber(Isolate* isolate, Handle<JSReceiver> options,
                     Handle<String> property) {
  Handle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value, JSReceiver::GetProperty(isolate, options, property),
      Nothing<int>());
  if (value->IsUndefined()) {
    return Nothing<int>();
  }
  // ToIntegerOrInfinity
  // 1. Let number be ? ToNumber(argument).
  Handle<Object> value_num;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value_num, Object::ToNumber(isolate, value), Nothing<int>());
  DCHECK(value_num->IsNumber());

  // 2. If number is NaN, +0𝔽, or -0𝔽, return 0.
  if (value_num->IsNaN()) {
    return Just(0);
  }
  // 3. If number is +∞𝔽, return +∞.
  // 4. If number is -∞𝔽, return -∞.
  // 5. Let integer be floor(abs(ℝ(number))).
  // 6. If number < +0𝔽, set integer to -integer.
  // 7. Return integer.
  return Just(FastD2I(floor(abs(value_num->Number()))));
}

}  // namespace

template <typename T>
MaybeHandle<T> FormatCommon(
    Isolate* isolate, Handle<Object> value_obj, Handle<JSDurationFormat> format,
    const char* func_name,
    MaybeHandle<T> (*formatToResult)(Isolate*,
                                     const icu::number::FormattedNumber&)) {
  icu::number::LocalizedNumberFormatter* icu_number_formatter =
      format->icu_number_formatter().raw();
  DCHECK_NOT_NULL(icu_number_formatter);

  Handle<JSReceiver> object;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, object,
                             Object::ToObject(isolate, value_obj), T);

  Factory* factory = isolate->factory();
  // We need to ACCESS the value_obj by the following the ORDER specified
  // in ecma262 #table-temporal-temporaldurationlike-properties
  // Table : Properties of a TemporalDurationLike
  // Internal SlotProperty
  // [[Days]] "days"
  Maybe<int> maybe_days = GetNumber(isolate, object, factory->days_string());
  // [[Hours]] "hours"
  Maybe<int> maybe_hours = GetNumber(isolate, object, factory->hours_string());
  // [[Microseconds]] "microseconds"
  Maybe<int> maybe_microseconds =
      GetNumber(isolate, object, factory->microseconds_string());
  // [[Milliseconds]] "milliseconds"
  Maybe<int> maybe_milliseconds =
      GetNumber(isolate, object, factory->milliseconds_string());
  // [[Minutes]] "minutes"
  Maybe<int> maybe_minutes =
      GetNumber(isolate, object, factory->minutes_string());
  // [[Months]] "months"
  Maybe<int> maybe_months =
      GetNumber(isolate, object, factory->months_string());
  // [[Nanoseconds]] "nanoseconds"
  Maybe<int> maybe_nanoseconds =
      GetNumber(isolate, object, factory->nanoseconds_string());
  // [[Seconds]] "seconds"
  Maybe<int> maybe_seconds =
      GetNumber(isolate, object, factory->seconds_string());
  // [[Weeks]] "weeks"
  Maybe<int> maybe_weeks = GetNumber(isolate, object, factory->weeks_string());
  // [[Years]] "years"
  Maybe<int> maybe_years = GetNumber(isolate, object, factory->years_string());

  // 5. If any is false, then
  // a. Throw a TypeError exception.
  if (maybe_years.IsNothing() && maybe_months.IsNothing() &&
      maybe_weeks.IsNothing() && maybe_days.IsNothing() &&
      maybe_hours.IsNothing() && maybe_minutes.IsNothing() &&
      maybe_seconds.IsNothing() && maybe_milliseconds.IsNothing() &&
      maybe_microseconds.IsNothing() && maybe_nanoseconds.IsNothing()) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(
            MessageTemplate::kMethodInvokedOnWrongType,
            factory->string_string()),  // TODO(ftang) change 2nd parameter
        T);
  }
  double scales[JSDurationFormat::Field::kCount] = {
      365.0,                                             // kYears
      30.0,                                              // kMonths
      7.0,                                               // kWeeks
      1.0,                                               // kDays
      (1.0 / 24),                                        // kHours
      (1.0 / (24 * 60)),                                 // kMinutes
      (1.0 / (24 * 60 * 60)),                            // kSeconds
      (1.0 / (24 * 60 * 60 * 1000.0)),                   // kMilliseconds
      (1.0 / (24 * 60 * 60 * 1000.0 * 1000.0)),          // kMicroseconds
      (1.0 / (24 * 60 * 60 * 1000.0 * 1000.0 * 1000.0))  // kNanoseconds
  };
  double scale = scales[format->largest_unit()];

  double number = (maybe_years.IsJust())
                      ? (scales[JSDurationFormat::Field::kYears] / scale) *
                            maybe_years.FromJust()
                      : 0;
  number += (maybe_months.IsJust())
                ? (scales[JSDurationFormat::Field::kMonths] / scale) *
                      maybe_months.FromJust()
                : 0;
  number += (maybe_weeks.IsJust())
                ? (scales[JSDurationFormat::Field::kWeeks] / scale) *
                      maybe_weeks.FromJust()
                : 0;
  number += (maybe_days.IsJust())
                ? (scales[JSDurationFormat::Field::kDays] / scale) *
                      maybe_days.FromJust()
                : 0;
  number += (maybe_hours.IsJust())
                ? (scales[JSDurationFormat::Field::kHours] / scale) *
                      maybe_hours.FromJust()
                : 0;
  number += (maybe_minutes.IsJust())
                ? (scales[JSDurationFormat::Field::kMinutes] / scale) *
                      maybe_minutes.FromJust()
                : 0;
  number += (maybe_seconds.IsJust())
                ? (scales[JSDurationFormat::Field::kSeconds] / scale) *
                      maybe_seconds.FromJust()
                : 0;
  number += (maybe_milliseconds.IsJust())
                ? (scales[JSDurationFormat::Field::kMilliseconds] / scale) *
                      maybe_milliseconds.FromJust()
                : 0;
  number += (maybe_microseconds.IsJust())
                ? (scales[JSDurationFormat::Field::kMicroseconds] / scale) *
                      maybe_microseconds.FromJust()
                : 0;
  number += (maybe_nanoseconds.IsJust())
                ? (scales[JSDurationFormat::Field::kNanoseconds] / scale) *
                      maybe_nanoseconds.FromJust()
                : 0;

  UErrorCode status = U_ZERO_ERROR;
  icu::number::FormattedNumber formatted =
      icu_number_formatter->formatDouble(number, status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError), T);
  }

  return formatToResult(isolate, formatted);
}

MaybeHandle<String> FormatToString(
    Isolate* isolate, const icu::number::FormattedNumber& formatted) {
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString result = formatted.toString(status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError), String);
  }
  return Intl::ToString(isolate, result);
}

MaybeHandle<JSArray> FormatToJSArray(
    Isolate* isolate, const icu::number::FormattedNumber& formatted) {
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString string = formatted.toString(status);

  Factory* factory = isolate->factory();
  Handle<JSArray> array = factory->NewJSArray(0);
  icu::ConstrainedFieldPosition cfpos;

  while (formatted.nextPosition(cfpos, status) && U_SUCCESS(status)) {
    printf("category=%d field=%d start=%d limit=%d\n", cfpos.getCategory(),
           cfpos.getField(), cfpos.getStart(), cfpos.getLimit());
  }
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError), JSArray);
  }

  JSObject::ValidateElements(*array);
  return array;
}

MaybeHandle<String> JSDurationFormat::Format(Isolate* isolate,
                                             Handle<Object> value_obj,
                                             Handle<JSDurationFormat> format) {
  return FormatCommon<String>(isolate, value_obj, format,
                              "Intl.DurationFormat.prototype.format",
                              FormatToString);
}

MaybeHandle<JSArray> JSDurationFormat::FormatToParts(
    Isolate* isolate, Handle<Object> value_obj,
    Handle<JSDurationFormat> format) {
  return FormatCommon<JSArray>(isolate, value_obj, format,
                               "Intl.DurationFormat.prototype.format",
                               FormatToJSArray);
}

const std::set<std::string>& JSDurationFormat::GetAvailableLocales() {
  return JSNumberFormat::GetAvailableLocales();
}

Handle<String> JSDurationFormat::StyleAsString() const {
  // TODO(ftang)
  return GetReadOnlyRoots().long_string_handle();
}

}  // namespace internal
}  // namespace v8
