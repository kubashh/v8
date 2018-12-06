// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-locale.h"

#include <memory>
#include <string>
#include <vector>

#include "src/api.h"
#include "src/global-handles.h"
#include "src/heap/factory.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-locale-inl.h"
#include "unicode/char16ptr.h"
#include "unicode/locid.h"
#include "unicode/uloc.h"

namespace v8 {
namespace internal {

namespace {
std::string LocaleToString(const icu::Locale& locale) {
  CHECK(locale.isBogus() == FALSE);
  UErrorCode status = U_ZERO_ERROR;
  char buffer[ULOC_FULLNAME_CAPACITY];
  uloc_toLanguageTag(locale.getName(), buffer, ULOC_FULLNAME_CAPACITY, TRUE,
                     &status);
  CHECK(U_SUCCESS(status));
  return buffer;
}

std::string LocaleGetBaseName(const icu::Locale& locale) {
  CHECK(locale.isBogus() == FALSE);
  UErrorCode status = U_ZERO_ERROR;
  char buffer[ULOC_FULLNAME_CAPACITY];
  uloc_toLanguageTag(locale.getBaseName(), buffer, ULOC_FULLNAME_CAPACITY, TRUE,
                     &status);
  CHECK(U_SUCCESS(status));
  return buffer;
}

std::string LocaleGetUnicodeKeywordValue(const icu::Locale& locale,
                                         const char* key) {
  CHECK(locale.isBogus() == FALSE);
  // ICU63 Replace with
  // locale.getUnicodeKeywordValue(&status)
  UErrorCode status = U_ZERO_ERROR;
  char buffer[ULOC_FULLNAME_CAPACITY];
  int32_t len = locale.getKeywordValue(uloc_toLegacyKey(key), buffer,
                                       ULOC_FULLNAME_CAPACITY, status);
  buffer[len] = '\0';  // null terminate
  CHECK(U_SUCCESS(status));
  if (len > 0) {
    return uloc_toUnicodeLocaleType(key, buffer);
  } else {
    return "";
  }
}

}  // namespace

MaybeHandle<JSLocale> JSLocale::Initialize(Isolate* isolate,
                                           Handle<JSLocale> locale_holder,
                                           Handle<String> locale,
                                           Handle<JSReceiver> options) {
  static const char* const kMethod = "Intl.Locale";
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  UErrorCode status = U_ZERO_ERROR;

  // Get ICU locale format, and canonicalize it.
  char icu_result[ULOC_FULLNAME_CAPACITY];

  if (locale->length() == 0) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kLocaleNotEmpty),
                    JSLocale);
  }

  v8::String::Utf8Value bcp47_locale(v8_isolate, v8::Utils::ToLocal(locale));
  CHECK_LT(0, bcp47_locale.length());
  CHECK_NOT_NULL(*bcp47_locale);

  int parsed_length = 0;
  int icu_length =
      uloc_forLanguageTag(*bcp47_locale, icu_result, ULOC_FULLNAME_CAPACITY,
                          &parsed_length, &status);

  if (U_FAILURE(status) ||
      parsed_length < static_cast<int>(bcp47_locale.length()) ||
      status == U_STRING_NOT_TERMINATED_WARNING || icu_length == 0) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kLocaleBadParameters,
                      isolate->factory()->NewStringFromAsciiChecked(kMethod),
                      locale_holder),
        JSLocale);
  }

  // TODO(ftang) create the locale in a better way
  icu::Locale icu_locale(icu_result);
  if (icu_locale.isBogus()) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kLocaleBadParameters,
                      isolate->factory()->NewStringFromAsciiChecked(kMethod),
                      locale_holder),
        JSLocale);
  }

  // 31. Set locale.[[Locale]] to r.[[locale]].
  Handle<Managed<icu::Locale>> managed_locale =
      Managed<icu::Locale>::FromRawPtr(isolate, 0, icu_locale.clone());
  locale_holder->set_icu_locale(*managed_locale);

  // 32. Set locale.[[Calendar]] to r.[[ca]].
  // 33. Set locale.[[Collation]] to r.[[co]].
  // 34. Set locale.[[HourCycle]] to r.[[hc]].
  // 35. If relevantExtensionKeys contains "kf", then
  // a. Set locale.[[CaseFirst]] to r.[[kf]].
  // 36. If relevantExtensionKeys contains "kn", then
  // a. Set locale.[[Numeric]] to ! SameValue(r.[[kn]], "true").
  // 37. Set locale.[[NumberingSystem]] to r.[[nu]].
  // 38. Return locale.
  return locale_holder;
}

namespace {
Handle<String> UnicodeKeywordValue(Isolate* isolate, Handle<JSLocale> locale,
                                   const char* key) {
  return isolate->factory()->NewStringFromAsciiChecked(
      LocaleGetUnicodeKeywordValue(*(locale->icu_locale()->raw()), key)
          .c_str());
}
}  // namespace

Handle<Object> JSLocale::Language(Isolate* isolate, Handle<JSLocale> locale) {
  Factory* factory = isolate->factory();
  const char* language = locale->icu_locale()->raw()->getLanguage();
  if (language == nullptr || *language == '\0') {
    return factory->undefined_value();
  }
  return factory->NewStringFromAsciiChecked(language);
}

Handle<Object> JSLocale::Script(Isolate* isolate, Handle<JSLocale> locale) {
  Factory* factory = isolate->factory();
  const char* script = locale->icu_locale()->raw()->getScript();
  if (script == nullptr || *script == '\0') {
    return factory->undefined_value();
  }
  return factory->NewStringFromAsciiChecked(script);
}

Handle<Object> JSLocale::Region(Isolate* isolate, Handle<JSLocale> locale) {
  Factory* factory = isolate->factory();
  const char* region = locale->icu_locale()->raw()->getCountry();
  if (region == nullptr || *region == '\0') {
    return factory->undefined_value();
  }
  return factory->NewStringFromAsciiChecked(region);
}

Handle<String> JSLocale::BaseName(Isolate* isolate, Handle<JSLocale> locale) {
  return isolate->factory()->NewStringFromAsciiChecked(
      LocaleGetBaseName(*(locale->icu_locale()->raw())).c_str());
}

Handle<Object> JSLocale::Calendar(Isolate* isolate, Handle<JSLocale> locale) {
  return UnicodeKeywordValue(isolate, locale, "ca");
}

Handle<Object> JSLocale::CaseFirst(Isolate* isolate, Handle<JSLocale> locale) {
  return UnicodeKeywordValue(isolate, locale, "kf");
}

Handle<Object> JSLocale::Collation(Isolate* isolate, Handle<JSLocale> locale) {
  return UnicodeKeywordValue(isolate, locale, "co");
}

Handle<Object> JSLocale::HourCycle(Isolate* isolate, Handle<JSLocale> locale) {
  return UnicodeKeywordValue(isolate, locale, "hc");
}

Handle<Object> JSLocale::Numeric(Isolate* isolate, Handle<JSLocale> locale) {
  Factory* factory = isolate->factory();
  std::string numeric =
      LocaleGetUnicodeKeywordValue(*(locale->icu_locale()->raw()), "kn");
  return (numeric == "true") ? factory->true_value() : factory->false_value();
}

Handle<Object> JSLocale::NumberingSystem(Isolate* isolate,
                                         Handle<JSLocale> locale) {
  return UnicodeKeywordValue(isolate, locale, "nu");
}

Handle<String> JSLocale::ToString(Isolate* isolate, Handle<JSLocale> locale) {
  return isolate->factory()->NewStringFromAsciiChecked(
      LocaleToString(*(locale->icu_locale()->raw())).c_str());
}
namespace {

Handle<String> MorphLocale(Isolate* isolate, String language_tag,
                           int32_t (*morph_func)(const char*, char*, int32_t,
                                                 UErrorCode*)) {
  Factory* factory = isolate->factory();
  char localeBuffer[ULOC_FULLNAME_CAPACITY];
  char morphBuffer[ULOC_FULLNAME_CAPACITY];

  UErrorCode status = U_ZERO_ERROR;
  // Convert from language id to locale.
  int32_t parsed_length;
  int32_t length =
      uloc_forLanguageTag(language_tag->ToCString().get(), localeBuffer,
                          ULOC_FULLNAME_CAPACITY, &parsed_length, &status);
  CHECK(parsed_length == language_tag->length());
  DCHECK(U_SUCCESS(status));
  DCHECK_GT(length, 0);
  DCHECK_NOT_NULL(morph_func);
  // Add the likely subtags or Minimize the subtags on the locale id
  length =
      (*morph_func)(localeBuffer, morphBuffer, ULOC_FULLNAME_CAPACITY, &status);
  DCHECK(U_SUCCESS(status));
  DCHECK_GT(length, 0);
  // Returns a well-formed language tag
  length = uloc_toLanguageTag(morphBuffer, localeBuffer, ULOC_FULLNAME_CAPACITY,
                              false, &status);
  DCHECK(U_SUCCESS(status));
  DCHECK_GT(length, 0);
  std::string lang(localeBuffer, length);
  std::replace(lang.begin(), lang.end(), '_', '-');

  return factory->NewStringFromAsciiChecked(lang.c_str());
}

}  // namespace

Handle<String> JSLocale::Maximize(Isolate* isolate, String locale) {
  return MorphLocale(isolate, locale, uloc_addLikelySubtags);
}

Handle<String> JSLocale::Minimize(Isolate* isolate, String locale) {
  return MorphLocale(isolate, locale, uloc_minimizeSubtags);
}

}  // namespace internal
}  // namespace v8
