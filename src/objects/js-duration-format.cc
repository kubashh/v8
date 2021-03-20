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
#include "src/objects/js-list-format.h"
#include "src/objects/js-number-format.h"
#include "src/objects/managed.h"
#include "src/objects/objects-inl.h"
#include "unicode/decimfmt.h"
#include "unicode/listformatter.h"
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

}  // namespace

class FormattedDuration : public icu::FormattedValue {
 public:
  FormattedDuration(icu::FormattedList&& value) : fValue(std::move(value)) {}
  FormattedDuration(FormattedDuration&& src) U_NOEXCEPT;
  virtual ~FormattedDuration() U_OVERRIDE {}
  FormattedDuration(const FormattedDuration&) = delete;
  FormattedDuration& operator=(const FormattedDuration&) = delete;
  FormattedDuration& operator=(FormattedDuration&& src) U_NOEXCEPT;
  icu::UnicodeString toString(UErrorCode& status) const U_OVERRIDE {
    return fValue.toString(status);
  }
  icu::UnicodeString toTempString(UErrorCode& status) const U_OVERRIDE {
    return fValue.toTempString(status);
  }
  icu::Appendable& appendTo(icu::Appendable& appendable,
                            UErrorCode& status) const U_OVERRIDE {
    return fValue.appendTo(appendable, status);
  }
  UBool nextPosition(icu::ConstrainedFieldPosition& cfpos,
                     UErrorCode& status) const U_OVERRIDE {
    return fValue.nextPosition(cfpos, status);
  }

 private:
  icu::FormattedList fValue;
};

class DurationFormatInternal {
 public:
  DurationFormatInternal(
      JSDurationFormat::Field smallest_unit,
      JSDurationFormat::Field largest_unit,
      const icu::number::LocalizedNumberFormatter& number_formatter)
      : smallest_(smallest_unit),
        largest_(largest_unit),
        number_formatter_(number_formatter) {}

  virtual ~DurationFormatInternal() = default;

  virtual FormattedDuration format(Maybe<int>* maybe_values,
                                   UErrorCode& status) const = 0;

  virtual Style style() const = 0;

  JSDurationFormat::Field smallest_unit() const { return smallest_; }
  JSDurationFormat::Field largest_unit() const { return largest_; }

  const icu::UnicodeString numberingSystem() const {
    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString skeleton = number_formatter_.toSkeleton(status);

    CHECK(U_SUCCESS(status));
    return Intl::NumberingSystemFromSkeleton(skeleton);
  }

 protected:
  JSDurationFormat::Field smallest_;
  JSDurationFormat::Field largest_;
  icu::number::LocalizedNumberFormatter number_formatter_;
};

class DigitalDurationFormat : public DurationFormatInternal {
 public:
  DigitalDurationFormat(
      JSDurationFormat::Field smallest_unit,
      JSDurationFormat::Field largest_unit,
      const icu::number::LocalizedNumberFormatter& number_formatter,
      UErrorCode& status)
      : DurationFormatInternal(smallest_unit, largest_unit, number_formatter) {
    // TODO(ftang)
  }

  ~DigitalDurationFormat() override = default;

  FormattedDuration format(Maybe<int>* maybe_values,
                           UErrorCode& status) const override {
    icu::FormattedList result;
    // TODO(ftang)
    return FormattedDuration(std::move(result));
  }

  Style style() const override { return Style::kDotted; }
};

class ListDurationFormat : public DurationFormatInternal {
 public:
  ListDurationFormat(
      JSDurationFormat::Field smallest_unit,
      JSDurationFormat::Field largest_unit, icu::ListFormatter* list_formatter,
      const icu::number::LocalizedNumberFormatter& number_formatter,
      UErrorCode& status)
      : DurationFormatInternal(smallest_unit, largest_unit, number_formatter),
        list_formatter_(list_formatter) {
    std::vector<std::string> unit_str({"year", "month", "week", "day", "hour",
                                       "minute", "second", "millisecond",
                                       "microsecond", "nanosecond"});
    icu::number::LocalizedNumberFormatter formatter(number_formatter);
    for (int i = largest_; i <= smallest_; i++) {
      formatter =
          formatter.unit(icu::MeasureUnit::forIdentifier(unit_str[i], status));
      if (U_FAILURE(status)) {
        return;
      }
      number_formatters_.push_back(formatter);
    }
  }

  ~ListDurationFormat() override = default;

  Style style() const override {
    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString skeleton = number_formatter_.toSkeleton(status);
    CHECK(U_SUCCESS(status));
    // Ex: skeleton as
    // "unit/length-meter .### rounding-mode-half-up unit-width-full-name"
    if (skeleton.indexOf("unit-width-full-name") >= 0) {
      return Style::kLong;
    }
    // Ex: skeleton as
    // "unit/length-meter .### rounding-mode-half-up unit-width-narrow".
    if (skeleton.indexOf("unit-width-narrow") >= 0) {
      return Style::kNarrow;
    }
    // Ex: skeleton as
    // "unit/length-foot .### rounding-mode-half-up"
    return Style::kShort;
  }

  FormattedDuration format(Maybe<int>* maybe_values,
                           UErrorCode& status) const override {
    std::vector<icu::UnicodeString> array;
    for (int i = largest_; i <= smallest_; i++) {
      if (maybe_values[i].IsJust()) {
        int64_t v = maybe_values[i].FromJust();
        icu::number::FormattedNumber formatted =
            number_formatters_[i - largest_].formatInt(v, status);
        array.push_back(formatted.toString(status));
        if (U_FAILURE(status)) {
          break;
        }
      }
    }
    icu::FormattedList result;
    if (U_SUCCESS(status)) {
      result = list_formatter_->formatStringsToValue(
          array.data(), static_cast<int32_t>(array.size()), status);
    }
    return FormattedDuration(std::move(result));
  }

 private:
  std::unique_ptr<icu::ListFormatter> list_formatter_;
  std::vector<icu::number::LocalizedNumberFormatter> number_formatters_;
};

namespace {

UListFormatterWidth toListWidth(Style style) {
  switch (style) {
    case Style::kLong:
      return ULISTFMT_WIDTH_WIDE;
    case Style::kShort:
      return ULISTFMT_WIDTH_SHORT;
    case Style::kNarrow:
      return ULISTFMT_WIDTH_NARROW;
    default:
      break;
  }
  UNREACHABLE();
}

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

Maybe<bool> FieldsToUnits(Isolate* isolate, Handle<Object> fields,
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
    MAYBE_RETURN(is_array, Nothing<bool>());
    if (is_array.FromJust()) {
      // Set fields to ? CreateListFromArrayLike(fields).
      Handle<FixedArray> fields_list;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, fields_list,
          Object::CreateListFromArrayLike(isolate, fields, ElementTypes::kAll),
          Nothing<bool>());
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
        return Nothing<bool>();
      }
    } else {  // Else
      return Nothing<bool>();
    }
  }
  std::string units;
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
  return Just(true);
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

  icu::number::LocalizedNumberFormatter number_formatter =
      icu::number::NumberFormatter::withLocale(icu_locale)
          .roundingMode(UNUM_ROUND_HALFUP);

  // For 'latn' numbering system, skip the adoptSymbols which would cause
  // 10.1%-13.7% of regression of JSTests/Intl-NewIntlNumberFormat
  // See crbug/1052751 so we skip calling adoptSymbols and depending on the
  // default instead.
  if (!numbering_system.empty() && numbering_system != "latn") {
    number_formatter = number_formatter.adoptSymbols(
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

  // Let fields be ? Get(options, "fields").
  Handle<Object> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      Object::GetPropertyOrElement(isolate, options, factory->fields_string()),
      JSDurationFormat);

  Field largest_unit;
  Field smallest_unit;
  Maybe<bool> maybe_fields =
      FieldsToUnits(isolate, fields, &smallest_unit, &largest_unit);
  MAYBE_RETURN(maybe_fields, MaybeHandle<JSDurationFormat>());

  v8::internal::DurationFormatInternal* internal;
  if (style_enum == Style::kDotted) {
    internal = new v8::internal::DigitalDurationFormat(
        smallest_unit, largest_unit, number_formatter, status);
  } else {
    icu::ListFormatter* list_formatter = icu::ListFormatter::createInstance(
        icu_locale, ULISTFMT_TYPE_UNITS, toListWidth(style_enum), status);
    if (U_FAILURE(status) || list_formatter == nullptr) {
      delete list_formatter;
      THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError),
                      JSDurationFormat);
    }
    number_formatter = number_formatter.unitWidth(toWidth(style_enum));
    internal = new v8::internal::ListDurationFormat(
        smallest_unit, largest_unit, list_formatter, number_formatter, status);
    if (U_FAILURE(status) || internal == nullptr) {
      delete list_formatter;
    }
  }

  if (U_FAILURE(status) || internal == nullptr) {
    delete internal;
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError),
                    JSDurationFormat);
  }

  Handle<Managed<v8::internal::DurationFormatInternal>> managed_internal =
      Managed<v8::internal::DurationFormatInternal>::FromRawPtr(isolate, 0,
                                                                internal);

  Handle<JSDurationFormat> duration_format = Handle<JSDurationFormat>::cast(
      factory->NewFastOrSlowJSObjectFromMap(map));

  DisallowGarbageCollection no_gc;
  duration_format->set_flags(0);
  duration_format->set_locale(*locale_str);
  duration_format->set_internal(*managed_internal);
  duration_format->set_smallest_unit(smallest_unit);
  duration_format->set_largest_unit(largest_unit);

  return duration_format;
}

namespace {

Handle<JSArray> GetFields(Isolate* isolate,
                          JSDurationFormat::Field smallest_unit,
                          JSDurationFormat::Field largest_unit) {
  Factory* factory = isolate->factory();
  Handle<JSArray> array = factory->NewJSArray(0);
  int len = 0;
  for (int i = largest_unit; i <= smallest_unit; i++) {
    switch (i) {
      case JSDurationFormat::Field::kYears:
        JSObject::AddDataElement(array, len++, factory->years_string(), NONE);
        break;
      case JSDurationFormat::Field::kMonths:
        JSObject::AddDataElement(array, len++, factory->months_string(), NONE);
        break;
      case JSDurationFormat::Field::kWeeks:
        JSObject::AddDataElement(array, len++, factory->weeks_string(), NONE);
        break;
      case JSDurationFormat::Field::kDays:
        JSObject::AddDataElement(array, len++, factory->days_string(), NONE);
        break;
      case JSDurationFormat::Field::kHours:
        JSObject::AddDataElement(array, len++, factory->hours_string(), NONE);
        break;
      case JSDurationFormat::Field::kMinutes:
        JSObject::AddDataElement(array, len++, factory->minutes_string(), NONE);
        break;
      case JSDurationFormat::Field::kSeconds:
        JSObject::AddDataElement(array, len++, factory->seconds_string(), NONE);
        break;
      case JSDurationFormat::Field::kMilliseconds:
        JSObject::AddDataElement(array, len++, factory->milliseconds_string(),
                                 NONE);
        break;
      case JSDurationFormat::Field::kMicroseconds:
        JSObject::AddDataElement(array, len++, factory->microseconds_string(),
                                 NONE);
        break;
      case JSDurationFormat::Field::kNanoseconds:
        JSObject::AddDataElement(array, len++, factory->nanoseconds_string(),
                                 NONE);
        break;
    }
  }
  JSObject::ValidateElements(*array);
  return array;
}

Handle<String> StyleAsString(Isolate* isolate, Style style) {
  Factory* factory = isolate->factory();
  switch (style) {
    case Style::kLong:
      return factory->long_string();
    case Style::kShort:
      return factory->short_string();
    case Style::kNarrow:
      return factory->narrow_string();
    case Style::kDotted:
      return factory->dotted_string();
  }
  UNREACHABLE();
}
}  // namespace

Handle<JSObject> JSDurationFormat::ResolvedOptions(
    Isolate* isolate, Handle<JSDurationFormat> format) {
  Factory* factory = isolate->factory();
  v8::internal::DurationFormatInternal* internal = format->internal().raw();
  DCHECK_NOT_NULL(internal);
  Handle<JSObject> result = factory->NewJSObject(isolate->object_function());
  Handle<String> locale(format->locale(), isolate);
  const icu::UnicodeString numberingSystem_ustr = internal->numberingSystem();
  Handle<String> numberingSystem_string;
  CHECK(Intl::ToString(isolate, numberingSystem_ustr)
            .ToHandle(&numberingSystem_string));
  Handle<String> style = StyleAsString(isolate, internal->style());
  Handle<JSArray> fields =
      GetFields(isolate, internal->smallest_unit(), internal->largest_unit());

  JSObject::AddProperty(isolate, result, factory->locale_string(), locale,
                        NONE);
  JSObject::AddProperty(isolate, result, factory->numberingSystem_string(),
                        numberingSystem_string, NONE);
  JSObject::AddProperty(isolate, result, factory->style_string(), style, NONE);
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
    MaybeHandle<T> (*formatToResult)(Isolate*, const FormattedDuration&)) {
  v8::internal::DurationFormatInternal* internal = format->internal().raw();
  DCHECK_NOT_NULL(internal);

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

  UErrorCode status = U_ZERO_ERROR;
  Maybe<int> fields[] = {
      maybe_years,        maybe_months,     maybe_weeks,   maybe_days,
      maybe_hours,        maybe_minutes,    maybe_seconds, maybe_milliseconds,
      maybe_microseconds, maybe_nanoseconds};
  FormattedDuration formatted = internal->format(fields, status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError), T);
  }

  return formatToResult(isolate, formatted);
}

MaybeHandle<String> FormatToString(Isolate* isolate,
                                   const FormattedDuration& formatted) {
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString result = formatted.toString(status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError), String);
  }
  return Intl::ToString(isolate, result);
}

MaybeHandle<JSArray> FormatToJSArray(Isolate* isolate,
                                     const FormattedDuration& formatted) {
  Handle<JSArray> array = isolate->factory()->NewJSArray(0);
  icu::ConstrainedFieldPosition cfpos;
  cfpos.constrainCategory(UFIELD_CATEGORY_LIST);
  int index = 0;
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString string = formatted.toString(status);
  Handle<String> substring;
  while (formatted.nextPosition(cfpos, status) && U_SUCCESS(status)) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, substring,
        Intl::ToString(isolate, string, cfpos.getStart(), cfpos.getLimit()),
        JSArray);
    Intl::AddElement(isolate, array, index++,
                     JSListFormat::ToType(isolate, cfpos.getField()),
                     substring);
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

}  // namespace internal
}  // namespace v8
