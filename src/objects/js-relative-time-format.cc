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
  // Local scope for temporary handles.
  HandleScope handle_scope(isolate);

  // 1. If relativeTimeFormat has an [[InitializedIntlObject]] internal slot
  //    with value true, throw a TypeError exception.
  // 2. Set relativeTimeFormat.[[InitializedIntlObject]] to true.
  // 3. Let requestedLocales be ? CanonicalizeLocaleList(locales).

  // TODO(ftang): Replace the following with the correct Locale canonicalization
  // algorithm once it's ported to C++.
  Handle<String> locale = factory->NewStringFromStaticChars("en");
  if (input_locales->IsString()) {
    locale = Handle<String>::cast(input_locales);
  } else {
    Maybe<bool> is_array = Object::IsArray(input_locales);
    if (is_array.IsJust() && is_array.ToChecked()) {
      Handle<JSArray> array = Handle<JSArray>::cast(input_locales);
      if (array->GetElementsAccessor()->NumberOfElements(*array) > 0) {
        Handle<Object> first = array->GetElementsAccessor()->Get(array, 0);
        if (!first.is_null() && first->IsString()) {
          locale = Handle<String>::cast(first);
        }
      }
    }
  }

  // 4. If options is undefined, then
  Handle<JSReceiver> options;
  if (input_options->IsUndefined(isolate)) {
    //  a. Let options be ObjectCreate(null).
    options = isolate->factory()->NewJSObjectWithNullProto();
    // 5. Else
  } else {
    //  a. Let options be ? ToObject(options).
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, options,
                                     Object::ToObject(isolate, input_options),
                                     MaybeHandle<JSRelativeTimeFormat>());
  }
  // 6. Let opt be a new Record.

  // 7. Let matcher be ? GetOption(options, "localeMatcher", "string",
  //                               «"lookup", "best fit"»,  "best fit").
  Handle<String> service =
      factory->NewStringFromStaticChars("InitializeRelativeTimeFormat");
  Handle<FixedArray> locale_matcher_values = factory->NewFixedArray(2, TENURED);
  locale_matcher_values->set(0, *factory->lookup_string());
  locale_matcher_values->set(1, *factory->best_fit_string());

  Handle<Object> matcher_object;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, matcher_object,
      Object::GetOption(isolate, options, factory->localeMatcher_string(),
                        Object::OptionType::String, locale_matcher_values,
                        factory->best_fit_string(), service),
      MaybeHandle<JSRelativeTimeFormat>());

  // TODO(ftang) Implement the following locale matching algorith once the
  // ResolveLocale operation is implemented.
  // 8. Set opt.[[LocaleMatcher]] to matcher.
  // 9. Let localeData be %RelativeTimeFormat%.[[LocaleData]].
  // 10. Let r be ResolveLocale(%RelativeTimeFormat%.[[AvailableLocales]],
  //                            requestedLocales, opt,
  //                            %RelativeTimeFormat%.[[RelevantExtensionKeys]],
  //                            localeData).
  // 11. Let locale be r.[[Locale]].
  // 12. Set relativeTimeFormat.[[Locale]] to locale.
  relative_time_format_holder->set_locale(*locale);

  // 13. Let dataLocale be r.[[DataLocale]].

  Handle<FixedArray> style_values = factory->NewFixedArray(3, TENURED);
  style_values->set(0, *factory->long_string());
  style_values->set(1, *factory->short_string());
  style_values->set(2, *factory->narrow_string());

  // 14. Let s be ? GetOption(options, "style", "string",
  //                          «"long", "short", "narrow"», "long").
  Handle<Object> style_object;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, style_object,
      Object::GetOption(isolate, options, factory->style_string(),
                        Object::OptionType::String, style_values,
                        factory->long_string(), service),
      MaybeHandle<JSRelativeTimeFormat>());
  Handle<String> s = Handle<String>::cast(style_object);

  // 15. Set relativeTimeFormat.[[Style]] to s.
  Style style_enum =
      s->Equals(*factory->long_string())
          ? STYLE_LONG
          : (s->Equals(*factory->short_string()) ? STYLE_SHORT : STYLE_NARROW);
  relative_time_format_holder->set_style(style_enum);

  Handle<FixedArray> numeric_values = factory->NewFixedArray(2, TENURED);
  numeric_values->set(0, *factory->always_string());
  numeric_values->set(1, *factory->auto_string());

  // 16. Let numeric be ? GetOption(options, "numeric", "string",
  //                                «"always", "auto"», "always").
  Handle<Object> numeric_object;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, numeric_object,
      Object::GetOption(isolate, options, factory->numeric_string(),
                        Object::OptionType::String, numeric_values,
                        factory->always_string(), service),
      MaybeHandle<JSRelativeTimeFormat>());
  Handle<String> numeric = Handle<String>::cast(numeric_object);

  // 17. Set relativeTimeFormat.[[Numeric]] to numeric.
  relative_time_format_holder->set_numeric(
      numeric->Equals(*factory->always_string()) ? NUMERIC_ALWAYS
                                                 : NUMERIC_AUTO);

  // 18. Let fields be Get(localeData, dataLocale).
  // 19. Assert: fields is an object (see 1.3.3).
  // 20. Set relativeTimeFormat.[[Fields]] to fields.
  // 21. Let nfLocale be CreateArrayFromList(« locale »).
  // 22. Let nfOptions be ObjectCreate(%ObjectPrototype%).

  UErrorCode status = U_ZERO_ERROR;

  UDateRelativeDateTimeFormatterStyle icu_style_enum = getIcuStyle(style_enum);

  std::unique_ptr<char[]> locale_name = locale->ToCString();
  icu::Locale icu_locale(locale_name.get());

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

  // rule 26-29 are done inside RelativeDateTimeFormatter::init()
  // 26. Let prLocale be CreateArrayFromList(« locale »).
  // 27. Let prOptions be ObjectCreate(%ObjectPrototype%).
  // 28. Perform ! CreateDataPropertyOrThrow(prOptions, "style", "cardinal").
  // 29. Let relativeTimeFormat.[[PluralRules]] be
  //     ? Construct(%PluralRules%, « prLocale, prOptions »).

  // TODO(ftang) change UDISPCTX_CAPITALIZATION_NONE to other values if
  // ECMA402 later include option to change capitalization.
  Handle<Managed<icu::RelativeDateTimeFormatter>> managed_formatter =
      Managed<icu::RelativeDateTimeFormatter>::FromRawPtr(
          isolate, 0,
          new icu::RelativeDateTimeFormatter(
              icu_locale, number_format, icu_style_enum,
              UDISPCTX_CAPITALIZATION_NONE, status));

  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate,
                    NewRangeError(MessageTemplate::kLocaleBadParameters),
                    JSRelativeTimeFormat);
  }

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
