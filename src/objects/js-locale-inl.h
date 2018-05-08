// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#ifndef V8_OBJECTS_JS_LOCALE_INL_H_
#define V8_OBJECTS_JS_LOCALE_INL_H_

#include "src/objects/js-locale.h"

#include <map>
#include <memory>
#include <string>

#include "src/api.h"
#include "src/global-handles.h"
#include "src/heap/factory.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "unicode/locid.h"
#include "unicode/unistr.h"
#include "unicode/uvernum.h"
#include "unicode/uversion.h"

#if U_ICU_VERSION_MAJOR_NUM >= 59
#include "unicode/char16ptr.h"
#endif

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

constexpr std::array<std::pair<const char*, const char*>, 8>
    kOptionToUnicodeTagMap = {{{"calendar", "ca"},
                               {"collation", "co"},
                               {"currency", "cu"},
                               {"hourCycle", "hc"},
                               {"caseFirst", "kf"},
                               {"numeric", "kn"},
                               {"numberingSystem", "nu"},
                               {"timeZone", "tz"}}};

// Base locale accessors.
ACCESSORS(JSLocale, language, Object, kLanguageOffset);
ACCESSORS(JSLocale, script, Object, kScriptOffset);
ACCESSORS(JSLocale, region, Object, kRegionOffset);
ACCESSORS(JSLocale, base_name, Object, kBaseNameOffset);
ACCESSORS(JSLocale, locale, Object, kLocaleOffset);

// Unicode extension accessors.
ACCESSORS(JSLocale, calendar, Object, kCalendarOffset);
ACCESSORS(JSLocale, case_first, Object, kCaseFirstOffset);
ACCESSORS(JSLocale, collation, Object, kCollationOffset);
ACCESSORS(JSLocale, currency, Object, kCurrencyOffset);
ACCESSORS(JSLocale, hour_cycle, Object, kHourCycleOffset);
ACCESSORS(JSLocale, numeric, Object, kNumericOffset);
ACCESSORS(JSLocale, numbering_system, Object, kNumberingSystemOffset);
ACCESSORS(JSLocale, time_zone, Object, kTimeZoneOffset);

CAST_ACCESSOR(JSLocale);

bool JSLocale::InitializeLocale(Isolate* isolate,
                                Handle<JSObject> locale_holder,
                                Handle<String> locale,
                                Handle<JSReceiver> options) {
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  UErrorCode status = U_ZERO_ERROR;

  // Get ICU locale format, and canonicalize it.
  char icu_result[ULOC_FULLNAME_CAPACITY];
  char icu_canonical[ULOC_FULLNAME_CAPACITY];

  v8::String::Utf8Value bcp47_locale(v8_isolate, v8::Utils::ToLocal(locale));
  if (bcp47_locale.length() == 0) return false;

  int icu_length = uloc_forLanguageTag(
      *bcp47_locale, icu_result, ULOC_FULLNAME_CAPACITY, nullptr, &status);

  if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING ||
      icu_length == 0) {
    return false;
  }

  if (!JSLocale::InsertOptionsIntoLocale(isolate, options, icu_result)) {
    return false;
  }

  uloc_canonicalize(icu_result, icu_canonical, ULOC_FULLNAME_CAPACITY, &status);
  if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
    return false;
  }

  if (!JSLocale::PopulateLocaleWithUnicodeTags(isolate, icu_canonical,
                                               locale_holder)) {
    return false;
  }

  // Extract language, script and region parts.
  char icu_language[ULOC_LANG_CAPACITY];
  uloc_getLanguage(icu_canonical, icu_language, ULOC_LANG_CAPACITY, &status);

  char icu_script[ULOC_SCRIPT_CAPACITY];
  uloc_getScript(icu_canonical, icu_script, ULOC_SCRIPT_CAPACITY, &status);

  char icu_region[ULOC_COUNTRY_CAPACITY];
  uloc_getCountry(icu_canonical, icu_region, ULOC_COUNTRY_CAPACITY, &status);

  if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
    return false;
  }

  Factory* factory = isolate->factory();

  Handle<JSLocale>::cast(locale_holder)
      ->set_language(*factory->NewStringFromAsciiChecked(icu_language));
  if (strlen(icu_script) != 0) {
    Handle<JSLocale>::cast(locale_holder)
        ->set_script(*factory->NewStringFromAsciiChecked(icu_script));
  }
  if (strlen(icu_region) != 0) {
    Handle<JSLocale>::cast(locale_holder)
        ->set_region(*factory->NewStringFromAsciiChecked(icu_region));
  }

  char icu_base_name[ULOC_FULLNAME_CAPACITY];
  uloc_getBaseName(icu_canonical, icu_base_name, ULOC_FULLNAME_CAPACITY,
                   &status);
  // We need to convert it back to BCP47.
  char bcp47_result[ULOC_FULLNAME_CAPACITY];
  uloc_toLanguageTag(icu_base_name, bcp47_result, ULOC_FULLNAME_CAPACITY, true,
                     &status);
  if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
    return false;
  }

  Handle<JSLocale>::cast(locale_holder)
      ->set_base_name(*factory->NewStringFromAsciiChecked(bcp47_result));

  // Produce final representation of the locale string, for toString().
  uloc_toLanguageTag(icu_canonical, bcp47_result, ULOC_FULLNAME_CAPACITY, true,
                     &status);
  if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
    return false;
  }

  Handle<JSLocale>::cast(locale_holder)
      ->set_locale(*factory->NewStringFromAsciiChecked(bcp47_result));

  return true;
}

bool JSLocale::ExtractStringSetting(Isolate* isolate,
                                    Handle<JSReceiver> options, const char* key,
                                    icu::UnicodeString* setting) {
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  Handle<String> str = isolate->factory()->NewStringFromAsciiChecked(key);
  Handle<Object> object =
      JSReceiver::GetProperty(options, str).ToHandleChecked();
  if (object->IsString()) {
    v8::String::Utf8Value utf8_string(
        v8_isolate, v8::Utils::ToLocal(Handle<String>::cast(object)));
    *setting = icu::UnicodeString::fromUTF8(*utf8_string);
    return true;
  }
  return false;
}

bool JSLocale::InsertOptionsIntoLocale(Isolate* isolate,
                                       Handle<JSReceiver> options,
                                       char* icu_locale) {
  CHECK(isolate);
  CHECK(icu_locale);

  for (const auto& option_to_bcp47 : kOptionToUnicodeTagMap) {
    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString value_unicode;
    if (!ExtractStringSetting(isolate, options, option_to_bcp47.first,
                              &value_unicode)) {
      // Skip this key, user didn't specify it in options.
      continue;
    }
    std::string value_string;
    value_unicode.toUTF8String(value_string);

    // Convert bcp47 key and value into legacy ICU format so we can use
    // uloc_setKeywordValue.
    const char* key = uloc_toLegacyKey(option_to_bcp47.second);
    if (!key) return false;

    // Overwrite existing, or insert new key-value to the locale string.
    const char* value = uloc_toLegacyType(key, value_string.c_str());
    if (value) {
      // TODO(cira): ICU puts artificial limit on locale length, while BCP47
      // doesn't. Switch to C++ API when it's ready.
      // Related ICU bug - https://ssl.icu-project.org/trac/ticket/13417.
      uloc_setKeywordValue(key, value, icu_locale, ULOC_FULLNAME_CAPACITY,
                           &status);
      if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
        return false;
      }
    } else {
      return false;
    }
  }

  return true;
}

bool JSLocale::PopulateLocaleWithUnicodeTags(Isolate* isolate,
                                             const char* icu_locale,
                                             Handle<JSObject> locale_holder) {
  CHECK(isolate);
  CHECK(icu_locale);

  Factory* factory = isolate->factory();

  static std::map<std::string, std::string> bcp47_to_option_map;
  for (const auto& option_to_bcp47 : kOptionToUnicodeTagMap) {
    bcp47_to_option_map.emplace(option_to_bcp47.second, option_to_bcp47.first);
  }

  UErrorCode status = U_ZERO_ERROR;
  UEnumeration* keywords = uloc_openKeywords(icu_locale, &status);
  if (!keywords) return true;

  char value[ULOC_FULLNAME_CAPACITY];
  const char* keyword = uenum_next(keywords, nullptr, &status);
  while (keyword != nullptr) {
    status = U_ZERO_ERROR;
    uloc_getKeywordValue(icu_locale, keyword, value, ULOC_FULLNAME_CAPACITY,
                         &status);
    // Ignore those we don't recognize - spec allows that.
    const char* bcp47_key = uloc_toUnicodeLocaleKey(keyword);
    if (bcp47_key) {
      const char* bcp47_value = uloc_toUnicodeLocaleType(bcp47_key, value);
      if (bcp47_value) {
        auto iterator = bcp47_to_option_map.find(bcp47_key);
        if (iterator != bcp47_to_option_map.end()) {
          if (iterator->second == "numeric") {
            bool numeric = strcmp(bcp47_value, "true") == 0 ? true : false;
            Handle<JSLocale>::cast(locale_holder)
                ->set_numeric(*factory->ToBoolean(numeric));
          } else if (iterator->second == "calendar") {
            Handle<JSLocale>::cast(locale_holder)
                ->set_calendar(
                    *factory->NewStringFromAsciiChecked(bcp47_value));
          } else if (iterator->second == "caseFirst") {
            Handle<JSLocale>::cast(locale_holder)
                ->set_case_first(
                    *factory->NewStringFromAsciiChecked(bcp47_value));
          } else if (iterator->second == "collation") {
            Handle<JSLocale>::cast(locale_holder)
                ->set_collation(
                    *factory->NewStringFromAsciiChecked(bcp47_value));
          } else if (iterator->second == "currency") {
            Handle<JSLocale>::cast(locale_holder)
                ->set_currency(
                    *factory->NewStringFromAsciiChecked(bcp47_value));
          } else if (iterator->second == "hourCycle") {
            Handle<JSLocale>::cast(locale_holder)
                ->set_hour_cycle(
                    *factory->NewStringFromAsciiChecked(bcp47_value));
          } else if (iterator->second == "numberingSystem") {
            Handle<JSLocale>::cast(locale_holder)
                ->set_numbering_system(
                    *factory->NewStringFromAsciiChecked(bcp47_value));
          } else if (iterator->second == "timeZone") {
            Handle<JSLocale>::cast(locale_holder)
                ->set_time_zone(
                    *factory->NewStringFromAsciiChecked(bcp47_value));
          }
        }
      }
    }
    keyword = uenum_next(keywords, nullptr, &status);
  }

  uenum_close(keywords);

  return true;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_JS_LOCALE_INL_H_
