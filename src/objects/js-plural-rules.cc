// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-plural-rules.h"

#include "src/isolate-inl.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-plural-rules-inl.h"
#include "unicode/decimfmt.h"
#include "unicode/locid.h"
#include "unicode/numfmt.h"
#include "unicode/plurrule.h"
#include "unicode/strenum.h"

namespace v8 {
namespace internal {

namespace {

bool CreateICUPluralRules(Isolate* isolate, const icu::Locale& icu_locale,
                          const char* type_string, icu::PluralRules** pl,
                          icu::DecimalFormat** nf) {
  // Make formatter from options. Numbering system is added
  // to the locale as Unicode extension (if it was specified at all).
  UErrorCode status = U_ZERO_ERROR;

  UPluralType type = UPLURAL_TYPE_CARDINAL;
  if (strcmp(type_string, "ordinal") == 0) {
    type = UPLURAL_TYPE_ORDINAL;
  } else {
    CHECK_EQ(0, strcmp(type_string, "cardinal"));
  }

  icu::PluralRules* plural_rules =
      icu::PluralRules::forLocale(icu_locale, type, status);

  if (U_FAILURE(status)) {
    delete plural_rules;
    return false;
  }

  icu::DecimalFormat* number_format = static_cast<icu::DecimalFormat*>(
      icu::NumberFormat::createInstance(icu_locale, UNUM_DECIMAL, status));

  if (U_FAILURE(status)) {
    delete plural_rules;
    delete number_format;
    return false;
  }

  *pl = plural_rules;
  *nf = number_format;

  return true;
}

void InitializeICUPluralRules(Isolate* isolate, Handle<String> locale,
                              const char* type, icu::PluralRules** plural_rules,
                              icu::DecimalFormat** number_format) {
  icu::Locale icu_locale = Intl::CreateICULocale(isolate, locale);
  DCHECK(!icu_locale.isBogus());

  bool success = CreateICUPluralRules(isolate, icu_locale, type, plural_rules,
                                      number_format);
  if (!success) {
    // Remove extensions and try again.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    success = CreateICUPluralRules(isolate, no_extension_locale, type,
                                   plural_rules, number_format);

    if (!success) {
      FATAL("Failed to create ICU PluralRules, are ICU data files missing?");
    }
  }

  CHECK_NOT_NULL(*plural_rules);
  CHECK_NOT_NULL(*number_format);
}

}  // namespace

// static
MaybeHandle<JSPluralRules> JSPluralRules::InitializePluralRules(
    Isolate* isolate, Handle<JSPluralRules> plural_rules,
    Handle<Object> locales, Handle<Object> options_obj) {
  // 1. Let requestedLocales be ? CanonicalizeLocaleList(locales).
  Handle<JSObject> requested_locales;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, requested_locales,
                             Intl::CanonicalizeLocaleList(isolate, locales),
                             JSPluralRules);

  // 2. If options is undefined, then
  if (options_obj->IsUndefined(isolate)) {
    // 2. a. Let options be ObjectCreate(null).
    options_obj = isolate->factory()->NewJSObjectWithNullProto();
  } else {
    // 3. Else
    // 3. a. Let options be ? ToObject(options).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, options_obj,
        Object::ToObject(isolate, options_obj, "Intl.PluralRules"),
        JSPluralRules);
  }

  // At this point, options_obj can either be a JSObject or a JSProxy only.
  Handle<JSReceiver> options = Handle<JSReceiver>::cast(options_obj);

  // TODO(gsathya): This is currently done as part of the
  // Intl::ResolveLocale call below. Fix this once resolveLocale is
  // changed to not do the lookup.
  //
  // 5. Let matcher be ? GetOption(options, "localeMatcher", "string",
  // « "lookup", "best fit" », "best fit").
  // 6. Set opt.[[localeMatcher]] to matcher.

  // 7. Let t be ? GetOption(options, "type", "string", « "cardinal",
  // "ordinal" », "cardinal").
  std::vector<const char*> values = {"cardinal", "ordinal"};
  std::unique_ptr<char[]> type_str = nullptr;
  const char* type_cstr = "cardinal";
  Maybe<bool> found = Intl::GetStringOption(isolate, options, "type", values,
                                            "Intl.PluralRules", &type_str);
  MAYBE_RETURN(found, MaybeHandle<JSPluralRules>());
  if (found.FromJust()) {
    type_cstr = type_str.get();
  }

  // 8. Set pluralRules.[[Type]] to t.
  Handle<String> type =
      isolate->factory()->NewStringFromAsciiChecked(type_cstr);
  plural_rules->set_type(*type);

  // Note: The spec says we should do ResolveLocale after performing
  // SetNumberFormatDigitOptions but we need the locale to create all
  // the ICU data structures.
  //
  // This isn't observable so we aren't violating the spec.

  // 11. Let r be ResolveLocale(%PluralRules%.[[AvailableLocales]],
  // requestedLocales, opt, %PluralRules%.[[RelevantExtensionKeys]],
  // localeData).
  Handle<JSObject> r;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, r,
      Intl::ResolveLocale(isolate, "pluralrules", requested_locales, options),
      JSPluralRules);

  Handle<String> locale_str = isolate->factory()->locale_string();
  Handle<Object> locale_obj = JSObject::GetDataProperty(r, locale_str);

  // The locale has to be a string. Either a user provided
  // canonicalized string or the default locale.
  CHECK(locale_obj->IsString());
  Handle<String> locale = Handle<String>::cast(locale_obj);

  // 12. Set pluralRules.[[Locale]] to the value of r.[[locale]].
  plural_rules->set_locale(*locale);

  icu::PluralRules* icu_plural_rules;
  icu::DecimalFormat* icu_decimal_format;
  InitializeICUPluralRules(isolate, locale, type_cstr, &icu_plural_rules,
                           &icu_decimal_format);
  CHECK_NOT_NULL(icu_plural_rules);
  CHECK_NOT_NULL(icu_decimal_format);

  Handle<Managed<icu::PluralRules>> managed_plural_rules =
      Managed<icu::PluralRules>::FromRawPtr(isolate, 0, icu_plural_rules);
  plural_rules->set_icu_plural_rules(*managed_plural_rules);

  Handle<Managed<icu::DecimalFormat>> managed_decimal_format =
      Managed<icu::DecimalFormat>::FromRawPtr(isolate, 0, icu_decimal_format);
  plural_rules->set_icu_decimal_format(*managed_decimal_format);

  // 9. Perform ? SetNumberFormatDigitOptions(pluralRules, options, 0, 3).
  Maybe<bool> done = Intl::SetNumberFormatDigitOptions(
      isolate, icu_decimal_format, options, 0, 3);
  MAYBE_RETURN(done, MaybeHandle<JSPluralRules>());

  // 13. Return pluralRules.
  return plural_rules;
}

MaybeHandle<String> JSPluralRules::ResolvePlural(
    Isolate* isolate, Handle<JSPluralRules> plural_rules,
    Handle<Object> number) {
  icu::PluralRules* icu_plural_rules = plural_rules->icu_plural_rules()->raw();
  CHECK_NOT_NULL(icu_plural_rules);

  icu::DecimalFormat* icu_decimal_format =
      plural_rules->icu_decimal_format()->raw();
  CHECK_NOT_NULL(icu_decimal_format);

  // Currently, PluralRules doesn't implement all the options for rounding that
  // the Intl spec provides; format and parse the number to round to the
  // appropriate amount, then apply PluralRules.
  //
  // TODO(littledan): If a future ICU version supports an extended API to avoid
  // this step, then switch to that API. Bug thread:
  // http://bugs.icu-project.org/trac/ticket/12763
  icu::UnicodeString rounded_string;
  icu_decimal_format->format(number->Number(), rounded_string);

  icu::Formattable formattable;
  UErrorCode status = U_ZERO_ERROR;
  icu_decimal_format->parse(rounded_string, formattable, status);
  CHECK(U_SUCCESS(status));

  double rounded = formattable.getDouble(status);
  CHECK(status);

  icu::UnicodeString result = icu_plural_rules->select(rounded);
  return isolate->factory()
      ->NewStringFromTwoByte(Vector<const uint16_t>(
          reinterpret_cast<const uint16_t*>(result.getBuffer()),
          result.length()))
      .ToHandleChecked();
}

Handle<JSObject> JSPluralRules::ResolvedOptions(
    Isolate* isolate, Handle<JSPluralRules> plural_rules) {
  Handle<JSObject> result =
      isolate->factory()->NewJSObject(isolate->object_function());

  Handle<String> locale_key =
      isolate->factory()->NewStringFromStaticChars("locale");
  Handle<String> locale_value(plural_rules->locale(), isolate);
  CHECK(JSReceiver::CreateDataProperty(isolate, result, locale_key,
                                       locale_value, kDontThrow)
            .FromJust());

  Handle<String> type_key =
      isolate->factory()->NewStringFromStaticChars("type");
  Handle<String> type_value(plural_rules->type(), isolate);
  CHECK(JSReceiver::CreateDataProperty(isolate, result, type_key, type_value,
                                       kDontThrow)
            .FromJust());

  icu::DecimalFormat* icu_decimal_format =
      plural_rules->icu_decimal_format()->raw();
  CHECK_NOT_NULL(icu_decimal_format);

  // This is a safe upcast.
  icu::NumberFormat* icu_number_format =
      reinterpret_cast<icu::NumberFormat*>(icu_decimal_format);

  int min_int_digits = icu_number_format->getMinimumIntegerDigits();
  Handle<Smi> min_int_digits_value(Smi::FromInt(min_int_digits), isolate);
  Handle<String> min_int_digits_key =
      isolate->factory()->NewStringFromStaticChars("minimumIntegerDigits");
  CHECK(JSReceiver::CreateDataProperty(isolate, result, min_int_digits_key,
                                       min_int_digits_value, kDontThrow)
            .FromJust());

  int min_fraction_digits = icu_number_format->getMinimumFractionDigits();
  Handle<Smi> min_fraction_digits_value(Smi::FromInt(min_fraction_digits),
                                        isolate);
  Handle<String> min_fraction_digits_key =
      isolate->factory()->NewStringFromStaticChars("minimumFractionDigits");
  CHECK(JSReceiver::CreateDataProperty(isolate, result, min_fraction_digits_key,
                                       min_fraction_digits_value, kDontThrow)
            .FromJust());

  int max_fraction_digits = icu_number_format->getMaximumFractionDigits();
  Handle<Smi> max_fraction_digits_value(Smi::FromInt(max_fraction_digits),
                                        isolate);
  Handle<String> max_fraction_digits_key =
      isolate->factory()->NewStringFromStaticChars("maximumFractionDigits");
  CHECK(JSReceiver::CreateDataProperty(isolate, result, max_fraction_digits_key,
                                       max_fraction_digits_value, kDontThrow)
            .FromJust());

  if (icu_decimal_format->areSignificantDigitsUsed()) {
    int min_significant_digits =
        icu_decimal_format->getMinimumSignificantDigits();
    Handle<Smi> min_significant_digits_value(
        Smi::FromInt(min_significant_digits), isolate);
    Handle<String> min_significant_digits_key =
        isolate->factory()->NewStringFromStaticChars(
            "minimumSignificantDigits");
    CHECK(JSReceiver::CreateDataProperty(
              isolate, result, min_significant_digits_key,
              min_significant_digits_value, kDontThrow)
              .FromJust());

    int max_significant_digits =
        icu_decimal_format->getMaximumSignificantDigits();
    Handle<Smi> max_significant_digits_value(
        Smi::FromInt(max_significant_digits), isolate);
    Handle<String> max_significant_digits_key =
        isolate->factory()->NewStringFromStaticChars(
            "maximumSignificantDigits");
    CHECK(JSReceiver::CreateDataProperty(
              isolate, result, max_significant_digits_key,
              max_significant_digits_value, kDontThrow)
              .FromJust());
  }

  // 6. Let pluralCategories be a List of Strings representing the
  // possible results of PluralRuleSelect for the selected locale pr.
  icu::PluralRules* icu_plural_rules = plural_rules->icu_plural_rules()->raw();
  CHECK_NOT_NULL(icu_plural_rules);

  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::StringEnumeration> categories(
      icu_plural_rules->getKeywords(status));
  CHECK(U_SUCCESS(status));

  Handle<ArrayList> plural_categories = ArrayList::New(isolate, 1);
  for (int32_t i = 0;; i++) {
    const icu::UnicodeString* category = categories->snext(status);
    CHECK(U_SUCCESS(status));
    if (category == nullptr) break;

    std::string keyword;
    Handle<String> value = isolate->factory()->NewStringFromAsciiChecked(
        category->toUTF8String(keyword).data());
    plural_categories = ArrayList::Add(isolate, plural_categories, value);
  }

  // 7. Perform ! CreateDataProperty(options, "pluralCategories",
  // CreateArrayFromList(pluralCategories)).
  Handle<FixedArray> plural_categories_elements =
      ArrayList::Elements(isolate, plural_categories);
  Handle<JSArray> plural_categories_value =
      isolate->factory()->NewJSArrayWithElements(plural_categories_elements);
  Handle<String> plural_categories_key =
      isolate->factory()->NewStringFromStaticChars("pluralCategories");
  CHECK(JSReceiver::CreateDataProperty(isolate, result, plural_categories_key,
                                       plural_categories_value, kDontThrow)
            .FromJust());

  return result;
}

}  // namespace internal
}  // namespace v8
