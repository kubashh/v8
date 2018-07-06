// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-relative-time-format.h"

#include <map>
#include <memory>
#include <string>

#include "src/api.h"
#include "src/elements.h"
#include "src/global-handles.h"
#include "src/heap/factory.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-relative-time-format-inl.h"
#include "unicode/locid.h"
#include "unicode/numfmt.h"
#include "unicode/reldatefmt.h"
#include "unicode/unistr.h"

namespace v8 {
namespace internal {

namespace {
UDateRelativeDateTimeFormatterStyle getIcuStyle(
    JSRelativeTimeFormat::Style style) {
  switch (style) {
    case JSRelativeTimeFormat::STYLE_LONG:
      return UDAT_STYLE_LONG;
    case JSRelativeTimeFormat::STYLE_SHORT:
      return UDAT_STYLE_SHORT;
    case JSRelativeTimeFormat::STYLE_NARROW:
      return UDAT_STYLE_NARROW;
    default:
      UNREACHABLE();
  }
}
}  // namespace

MaybeHandle<JSRelativeTimeFormat>
JSRelativeTimeFormat::InitializeRelativeTimeFormat(
    Isolate* isolate, Handle<JSRelativeTimeFormat> relative_time_format_holder,
    Handle<Object> input_locales, Handle<Object> input_options) {
  Factory* factory = isolate->factory();

  // 4. If options is undefined, then
  Handle<JSReceiver> options;
  if (input_options->IsUndefined(isolate)) {
    // a. Let options be ObjectCreate(null).
    options = isolate->factory()->NewJSObjectWithNullProto();
    // 5. Else
  } else {
    // a. Let options be ? ToObject(options).
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, options,
                                     Object::ToObject(isolate, input_options),
                                     MaybeHandle<JSRelativeTimeFormat>());
  }

  // 10. Let r be ResolveLocale(%RelativeTimeFormat%.[[AvailableLocales]],
  //                            requestedLocales, opt,
  //                            %RelativeTimeFormat%.[[RelevantExtensionKeys]],
  //                            localeData).
  Handle<JSObject> r;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, r,
      Intl::ResolveLocale(isolate, "relativetimeformat", input_locales,
                          options),
      MaybeHandle<JSRelativeTimeFormat>());
  Handle<String> locale;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, locale,
      Object::ToString(isolate,
                       JSObject::GetDataProperty(r, factory->locale_string())),
      MaybeHandle<JSRelativeTimeFormat>());

  // 11. Let locale be r.[[Locale]].
  // 12. Set relativeTimeFormat.[[Locale]] to locale.
  relative_time_format_holder->set_locale(*locale);

  // 13. Let dataLocale be r.[[DataLocale]].

  // 14. Let s be ? GetOption(options, "style", "string",
  //                          «"long", "short", "narrow"», "long").
  std::unique_ptr<char[]> style_str = nullptr;
  std::vector<const char*> style_values = {"long", "short", "narrow"};
  Maybe<bool> maybe_found_style =
      Intl::GetStringOption(isolate, options, "style", style_values,
                            "Intl.RelativeTimeFormat", &style_str);
  Style style_enum = STYLE_LONG;
  if (maybe_found_style.IsNothing()) {
    return MaybeHandle<JSRelativeTimeFormat>();
  }
  if (maybe_found_style.FromJust()) {
    DCHECK_NOT_NULL(style_str.get());
    style_enum = (strcmp(style_str.get(), "long") == 0)
                     ? STYLE_LONG
                     : ((strcmp(style_str.get(), "short") == 0) ? STYLE_SHORT
                                                                : STYLE_NARROW);
  }

  // 15. Set relativeTimeFormat.[[Style]] to s.
  relative_time_format_holder->set_style(style_enum);

  // 16. Let numeric be ? GetOption(options, "numeric", "string",
  //                                «"always", "auto"», "always").
  std::unique_ptr<char[]> numeric_str = nullptr;
  std::vector<const char*> numeric_values = {"always", "auto"};
  Maybe<bool> maybe_found_numeric =
      Intl::GetStringOption(isolate, options, "numeric", numeric_values,
                            "Intl.RelativeTimeFormat", &numeric_str);
  Numeric numeric_enum = NUMERIC_ALWAYS;
  if (maybe_found_numeric.IsNothing()) {
    return MaybeHandle<JSRelativeTimeFormat>();
  }
  if (maybe_found_numeric.FromJust()) {
    DCHECK_NOT_NULL(numeric_str.get());
    numeric_enum = (strcmp(numeric_str.get(), "always") == 0) ? NUMERIC_ALWAYS
                                                              : NUMERIC_AUTO;
  }

  // 17. Set relativeTimeFormat.[[Numeric]] to numeric.
  relative_time_format_holder->set_numeric(numeric_enum);

  std::unique_ptr<char[]> locale_name = locale->ToCString();
  icu::Locale icu_locale(locale_name.get());
  UErrorCode status = U_ZERO_ERROR;
  icu::RelativeDateTimeFormatter formatter(icu_locale, status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate,
                    NewRangeError(MessageTemplate::kLocaleBadParameters),
                    JSRelativeTimeFormat);
  }

  // 25. Let relativeTimeFormat.[[NumberFormat]] be
  //     ? Construct(%NumberFormat%, « nfLocale, nfOptions »).
  icu::NumberFormat* number_format =
      reinterpret_cast<icu::NumberFormat*>(formatter.getNumberFormat().clone());
  if (number_format != nullptr) {
    // 23. Perform ! CreateDataPropertyOrThrow(nfOptions, "useGrouping", false).
    number_format->setGroupingUsed(false);

    // 24. Perform ! CreateDataPropertyOrThrow(nfOptions,
    //                                         "minimumIntegerDigits", 2).
    number_format->setMinimumIntegerDigits(2);
  }

  // Change UDISPCTX_CAPITALIZATION_NONE to other values if
  // ECMA402 later include option to change capitalization.
  icu::RelativeDateTimeFormatter* icu_formatter =
      new icu::RelativeDateTimeFormatter(icu_locale, number_format,
                                         getIcuStyle(style_enum),
                                         UDISPCTX_CAPITALIZATION_NONE, status);

  if (U_FAILURE(status) || (icu_formatter == nullptr)) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kRelativeDateTimeFormatterBadParameters),
        JSRelativeTimeFormat);
  }
  Handle<Managed<icu::RelativeDateTimeFormatter>> managed_formatter =
      Managed<icu::RelativeDateTimeFormatter>::FromRawPtr(isolate, 0,
                                                          icu_formatter);

  // 30. Set relativeTimeFormat.[[InitializedRelativeTimeFormat]] to true.
  relative_time_format_holder->set_formatter(*managed_formatter);
  // 31. Return relativeTimeFormat.
  return relative_time_format_holder;
}

Handle<JSObject> JSRelativeTimeFormat::ResolvedOptions(
    Isolate* isolate, Handle<JSRelativeTimeFormat> format_holder) {
  Factory* factory = isolate->factory();
  Handle<JSObject> result = factory->NewJSObject(isolate->object_function());
  Handle<String> locale(format_holder->locale(), isolate);
  JSObject::AddProperty(isolate, result, factory->locale_string(), locale,
                        NONE);
  JSObject::AddProperty(isolate, result, factory->style_string(),
                        format_holder->StyleAsString(isolate), NONE);
  JSObject::AddProperty(isolate, result, factory->numeric_string(),
                        format_holder->NumericAsString(isolate), NONE);
  return result;
}

icu::RelativeDateTimeFormatter* JSRelativeTimeFormat::UnpackFormatter(
    Isolate* isolate, Handle<JSRelativeTimeFormat> holder) {
  return Managed<icu::RelativeDateTimeFormatter>::cast(holder->formatter())
      ->raw();
}

Handle<String> JSRelativeTimeFormat::StyleAsString(Isolate* isolate) const {
  Factory* factory = isolate->factory();
  switch (style()) {
    case STYLE_LONG:
      return factory->long_string();
    case STYLE_SHORT:
      return factory->short_string();
    case STYLE_NARROW:
      return factory->narrow_string();
    default:
      UNREACHABLE();
  }
}

Handle<String> JSRelativeTimeFormat::NumericAsString(Isolate* isolate) const {
  Factory* factory = isolate->factory();
  switch (numeric()) {
    case NUMERIC_ALWAYS:
      return factory->always_string();
    case NUMERIC_AUTO:
      return factory->auto_string();
    default:
      UNREACHABLE();
  }
}

}  // namespace internal
}  // namespace v8
