// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-locale.h"

#include <map>
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
#include "unicode/locid.h"
#include "unicode/regex.h"
#include "unicode/uloc.h"
#include "unicode/unistr.h"
#include "unicode/uvernum.h"
#include "unicode/uversion.h"

#if U_ICU_VERSION_MAJOR_NUM >= 59
#include "unicode/char16ptr.h"
#endif

namespace v8 {
namespace internal {

namespace {

struct OptionData {
  const char* name;
  const char* key;
  const std::vector<const char*>* possible_values;
  bool is_bool_value;
};

// Inserts tags from options into locale string.
Maybe<bool> InsertOptionsIntoLocale(Isolate* isolate,
                                    Handle<JSReceiver> options,
                                    char* icu_locale) {
  CHECK(isolate);
  CHECK(icu_locale);

  static std::vector<const char*> hour_cycle_values = {"h11", "h12", "h23",
                                                       "h24"};
  static std::vector<const char*> case_first_values = {"upper", "lower",
                                                       "false"};
  static std::vector<const char*> empty_values = {};
  static const std::array<OptionData, 6> kOptionToUnicodeTagMap = {
      {{"calendar", "ca", &empty_values, false},
       {"collation", "co", &empty_values, false},
       {"hourCycle", "hc", &hour_cycle_values, false},
       {"caseFirst", "kf", &case_first_values, false},
       {"numeric", "kn", &empty_values, true},
       {"numberingSystem", "nu", &empty_values, false}}};

  // TODO(cira): Pass in values as per the spec to make this to be
  // spec compliant.

  for (const auto& option_to_bcp47 : kOptionToUnicodeTagMap) {
    std::unique_ptr<char[]> value_str = nullptr;
    bool value_bool = false;
    Maybe<bool> maybe_found =
        option_to_bcp47.is_bool_value
            ? Intl::GetBoolOption(isolate, options, option_to_bcp47.name,
                                  "locale", &value_bool)
            : Intl::GetStringOption(isolate, options, option_to_bcp47.name,
                                    *(option_to_bcp47.possible_values),
                                    "locale", &value_str);
    if (maybe_found.IsNothing()) return maybe_found;

    // TODO(cira): Use fallback value if value is not found to make
    // this spec compliant.
    if (!maybe_found.FromJust()) continue;

    if (option_to_bcp47.is_bool_value) {
      value_str = value_bool ? isolate->factory()->true_string()->ToCString()
                             : isolate->factory()->false_string()->ToCString();
    }
    DCHECK_NOT_NULL(value_str.get());

    // Convert bcp47 key and value into legacy ICU format so we can use
    // uloc_setKeywordValue.
    const char* key = uloc_toLegacyKey(option_to_bcp47.key);
    DCHECK_NOT_NULL(key);

    // Overwrite existing, or insert new key-value to the locale string.
    const char* value = uloc_toLegacyType(key, value_str.get());
    UErrorCode status = U_ZERO_ERROR;
    if (value) {
      // TODO(cira): ICU puts artificial limit on locale length, while BCP47
      // doesn't. Switch to C++ API when it's ready.
      // Related ICU bug - https://ssl.icu-project.org/trac/ticket/13417.
      uloc_setKeywordValue(key, value, icu_locale, ULOC_FULLNAME_CAPACITY,
                           &status);
      if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
        return Just(false);
      }
    } else {
      return Just(false);
    }
  }

  return Just(true);
}

// TODO(gsathya): Dedupe to intl.h
bool IsLowerAscii(char c) { return c >= 'a' && c < 'z'; }
char AsciiToLower(char c) {
  if (c < 'A' || c > 'Z') {
    return c;
  }
  return c | (1 << 5);
}

// Assumes the input is a lowercase ascii string.
bool IsValidLanguageProduction(const std::string& tag) {
  // Based on https://tools.ietf.org/html/rfc5646#section-2.1:
  //
  // language  = 2*3ALPHA            ; shortest ISO 639 code
  //             ["-" extlang]       ; sometimes followed by
  //                                 ; extended language subtags
  //           / 4ALPHA              ; or reserved for future use
  //           / 5*8ALPHA            ; or registered language subtag
  //
  // extlang   = 3ALPHA              ; selected ISO 639 codes
  //             *2("-" 3ALPHA)      ; permanently reserved

  // The common case where the language tag is just two or three chars.
  size_t length = tag.length();
  if (V8_LIKELY(length == 2)) {
    return (IsLowerAscii(tag[0]) && IsLowerAscii(tag[1]));
  }

  if (V8_LIKELY(length == 3)) {
    return (IsLowerAscii(tag[0]) && IsLowerAscii(tag[1]) &&
            IsLowerAscii(tag[2]));
  }

  // This is complicated, let's use a regexp. Actually it's not too
  // bad, we should totally just write a bcp47 language parser in the
  // future.
  UErrorCode status = U_ZERO_ERROR;
  constexpr const char* kLanguageSubtagRegexp =
      "^(?:[a-z]{2,3})(?:-[a-z]{3}){0,3}$";
  std::unique_ptr<icu::RegexMatcher> language_subtag_matcher(
      new icu::RegexMatcher(
          icu::UnicodeString(kLanguageSubtagRegexp, -1, US_INV), 0, status));
  CHECK(U_SUCCESS(status));

  icu::UnicodeString tag_uni(tag.c_str(), -1, US_INV);
  language_subtag_matcher->reset(tag_uni);
  status = U_ZERO_ERROR;
  bool is_valid_lang_tag = language_subtag_matcher->matches(status);
  if (!is_valid_lang_tag || V8_UNLIKELY(U_FAILURE(status))) {
    return false;
  }

  return true;
}

// Assumes the input is a lowercase ascii string
bool IsValidScriptProduction(const std::string& tag) {
  // Based on https://tools.ietf.org/html/rfc5646#section-2.1:
  //
  // script        = 4ALPHA              ; ISO 15924 code

  if (V8_LIKELY(tag.length() == 4)) {
    return (IsLowerAscii(tag[0]) && IsLowerAscii(tag[1]) &&
            IsLowerAscii(tag[2]) && IsLowerAscii(tag[3]));
  }

  return false;
}

bool IsDigit(char digit) { return '0' <= digit && digit <= '9'; }

// Assumes the input is a lowercase ascii string
bool IsValidRegionProduction(const std::string& tag) {
  // Based on https://tools.ietf.org/html/rfc5646#section-2.1:
  //
  // region        = 2ALPHA              ; ISO 3166-1 code
  //               / 3DIGIT              ; UN M.49 code

  // region        = 2ALPHA              ; ISO 3166-1 code
  if (tag.length() == 2) {
    return (IsLowerAscii(tag[0]) && IsLowerAscii(tag[1]));
  }

  // region         = 3DIGIT              ; UN M.49 code
  if (tag.length() == 3) {
    return (IsDigit(tag[0]) && IsDigit(tag[1]) && IsDigit(tag[2]));
  }

  return false;
}

// Assumes the input is a lowercase ascii string
bool IsValidGrandfatheredProduction(const std::string& tag) {
  // Based on https://tools.ietf.org/html/rfc5646#section-2.1:
  //
  // grandfathered = irregular           ; non-redundant tags registered
  //               / regular             ; during the RFC 3066 era
  //
  // irregular     = "en-GB-oed"         ; irregular tags do not match
  //               / "i-ami"             ; the 'langtag' production and
  //               / "i-bnn"             ; would not otherwise be
  //               / "i-default"         ; considered 'well-formed'
  //               / "i-enochian"        ; These tags are all valid,
  //               / "i-hak"             ; but most are deprecated
  //               / "i-klingon"         ; in favor of more modern
  //               / "i-lux"             ; subtags or subtag
  //               / "i-mingo"           ; combination
  //               / "i-navajo"
  //               / "i-pwn"
  //               / "i-tao"
  //               / "i-tay"
  //               / "i-tsu"
  //               / "sgn-BE-FR"
  //               / "sgn-BE-NL"
  //               / "sgn-CH-DE"
  //
  // regular       = "art-lojban"        ; these tags match the 'langtag'
  //               / "cel-gaulish"       ; production, but their subtags
  //               / "no-bok"            ; are not extended language
  //               / "no-nyn"            ; or variant subtags: their meaning
  //               / "zh-guoyu"          ; is defined by their registration
  //               / "zh-hakka"          ; and all of these are deprecated
  //               / "zh-min"            ; in favor of a more modern
  //               / "zh-min-nan"        ; subtag or sequence of subtags
  //               / "zh-xiang"

  const char* tag_cstr = tag.c_str();

#define GRANDFATHERED_TAGS_LIST(V) \
  V("art-lojban", 10)              \
  V("cel-gaulish", 11)             \
  V("en-gb-oed", 9)                \
  V("i-ami", 5)                    \
  V("i-bnn", 5)                    \
  V("i-default", 9)                \
  V("i-enochian", 10)              \
  V("i-hak", 5)                    \
  V("i-klingon", 9)                \
  V("i-lux", 5)                    \
  V("i-mingo", 7)                  \
  V("i-navajo", 8)                 \
  V("i-pwn", 5)                    \
  V("i-tao", 5)                    \
  V("i-tay", 5)                    \
  V("i-tsu", 5)                    \
  V("no-bok", 6)                   \
  V("no-nyn", 6)                   \
  V("sgn-be-fr", 9)                \
  V("sgn-be-nl", 9)                \
  V("sgn-ch-de", 9)                \
  V("zh-guoyu", 8)                 \
  V("zh-hakka", 8)                 \
  V("zh-min", 6)                   \
  V("zh-min-nan", 10)              \
  V("zh-xiang", 8)

#define CHECK_IF_GRANDFATHERED_TAG(T, S) \
  if (strncmp(tag_cstr, T, S) == 0) {    \
    return true;                         \
  }
  GRANDFATHERED_TAGS_LIST(CHECK_IF_GRANDFATHERED_TAG);
  return false;
#undef GRANDFATHERED_TAGS_LIST
#undef CHECK_IF_GRANDFATHERED_TAG
}

// Assumes the input is a lowercase ascii string
bool IsValidPrivateUseProduction(const std::string& tag) {
  // Based on https://tools.ietf.org/html/rfc5646#section-2.1:
  //
  // privateuse    = "x" 1*("-" (1*8alphanum))

  // There's a bit of cheating going on here. Instead of doing a
  // complete check for the private use production, we shortcut with
  // just the first two chars. No other valid production can have
  // these two leading chars.
  //
  // So, this is either a valid private use tag or a completely
  // invalid language tag.
  //
  // All of the users of this method have already had the tag pass
  // through IsStructurallyValidLanguageTag, removing all the invalid
  // langauge tags, leaving us with just potential privateuse
  // tags. Voilà!
  if (tag.length() == 1 && tag[0] == 'x') return true;
  if (tag.length() == 2 && tag[0] == 'x' && tag[1] == '-') return true;

  return false;
}

Maybe<bool> ApplyOptionsToTag(Isolate* isolate, const std::string& tag,
                              Handle<JSReceiver> options) {
  // 1. Assert: Type(tag) is String.
  // 2. If IsStructurallyValidLanguageTag(tag) is false, throw a RangeError
  // exception.
  if (Int::IsStructurallyValidLanguageTag(isolate, tag)) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NewRangeError(MessageTemplate::kLocaleBadParameters),
        Nothing<bool>());
  }

  // 3. Let language be ? GetOption(options, "language", "string",
  //    undefined, undefined).
  std::vector<const char*> empty = {};
  std::unique_ptr<char[]> language_cstr = nullptr;
  Maybe<bool> found_language = Intl::GetStringOption(
      isolate, options, "language", empty, "Intl.Locale", &language_cstr);
  MAYBE_RETURN(found_language, Nothing<bool>());

  // 4. If language is not undefined, then
  if (found_language.FromJust()) {
    DCHECK_NOT_NULL(language_cstr.get());
    std::string language_str(language_cstr.get());
    std::transform(locale.begin(), locale.end(), locale.begin(), AsciiToLower);

    // 4. a. If language does not match the language production, throw
    // a RangeError exception.
    if (!IsValidLanguageProduction(language_str)) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate, NewRangeError(MessageTemplate::kLocaleBadParameters),
          Nothing<bool>());
    }

    // 4. b. If language matches the grandfathered production, throw a
    // RangeError exception.
    if (!IsValidGrandfatheredProduction(language_str)) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate, NewRangeError(MessageTemplate::kLocaleBadParameters),
          Nothing<bool>());
    }
  }

  // 5. Let script be ? GetOption(options, "script", "string",
  //    undefined, undefined).
  std::unique_ptr<char[]> script_cstr = nullptr;
  Maybe<bool> found_script = Intl::GetStringOption(
      isolate, options, "script", empty, "Intl.Locale", &script_cstr);
  MAYBE_RETURN(found_script, Nothing<bool>());

  // 6. If script is not undefined, then
  if (found.script.FromJust()) {
    DCHECK_NOT_NULL(script_cstr.get());
    std::string script_str(script_cstr.get());
    std::transform(locale.begin(), locale.end(), locale.begin(), AsciiToLower);

    // 6. a. If script does not match the script production, throw a
    // RangeError exception.
    if (!IsValidScriptProduction(script_str)) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate, NewRangeError(MessageTemplate::kLocaleBadParameters),
          Nothing<bool>());
    }
  }

  // 7. Let region be ? GetOption(options, "region", "string",
  //    undefined, undefined).
  std::unique_ptr<char[]> region_cstr = nullptr;
  Maybe<bool> found_region = Intl::GetStringOption(
      isolate, options, "region", empty, "Intl.Locale", &region_cstr);
  MAYBE_RETURN(found_region, Nothing<bool>());

  // 8. If region is not undefined, then
  if (found_region.FromJust()) {
    DCHECK_NOT_NULL(region_cstr.get());
    std::string region_str(region_cstr.get());
    std::transform(locale.begin(), locale.end(), locale.begin(), AsciiToLower);

    // 8. a. If region does not match the region production, throw a
    // RangeError exception.
    if (!IsValidRegionProduction(region_str)) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate, NewRangeError(MessageTemplate::kLocaleBadParameters),
          Nothing<bool>());
    }
  }

  // 9. If tag matches the grandfathered production,
  if (IsValidGrandfatheredProduction(tag)) {
    // 9. a. Set tag to CanonicalizeLanguageTag(tag).
    Maybe<std::string> maybe_tag = Intl::CanonicalizeLanguageTag(isolate, tag);
    MAYBE_RETURN(maybe_tag, Nothing<bool>());
    tag.assign(maybe_tag.FromJust());
  }

  // 10. If language is not undefined,
  if (found_language.FromJust()) {
    // TODO(gsathya): Dedupe this.
    DCHECK_NOT_NULL(language_cstr.get());
    std::string language_str(language_cstr.get());
    std::transform(language_str.begin(), language_str.end(), language_str.begin(), AsciiToLower);

    // 10. a. If tag matches the privateuse or grandfathered production,
    if (IsValidPrivateUseProduction(language_str) ||
        IsValidGrandfatheredProduction(language_str)) {
      // 10. a. i. Set tag to language.
      tag.assign(language_str);

      // 10. a. ii. If tag matches the grandfathered production,
      //     1. Set tag to CanonicalizeLanguageTag(tag).
      //
      // The above two steps aren't required. See
      // https://github.com/tc39/proposal-intl-locale/issues/52
    } else {
      // 10. b. Else,
      //    i. Assert: tag matches the langtag production.
      //   ii. Set tag to tag with the substring corresponding to the
      //       language production replaced by the string language.
    }
  }
}

// Fills in the JSLocale object slots with Unicode tag/values.
bool PopulateLocaleWithUnicodeTags(Isolate* isolate, const char* icu_locale,
                                   Handle<JSLocale> locale_holder) {
  CHECK(isolate);
  CHECK(icu_locale);

  Factory* factory = isolate->factory();

  UErrorCode status = U_ZERO_ERROR;
  UEnumeration* keywords = uloc_openKeywords(icu_locale, &status);
  if (!keywords) return true;

  char value[ULOC_FULLNAME_CAPACITY];
  while (const char* keyword = uenum_next(keywords, nullptr, &status)) {
    uloc_getKeywordValue(icu_locale, keyword, value, ULOC_FULLNAME_CAPACITY,
                         &status);
    if (U_FAILURE(status)) {
      status = U_ZERO_ERROR;
      continue;
    }

    // Ignore those we don't recognize - spec allows that.
    const char* bcp47_key = uloc_toUnicodeLocaleKey(keyword);
    if (bcp47_key) {
      const char* bcp47_value = uloc_toUnicodeLocaleType(bcp47_key, value);
      if (bcp47_value) {
          Handle<String> bcp47_handle =
              factory->NewStringFromAsciiChecked(bcp47_value);
          if (strncmp(bcp47_key, "kn", 2) == 0) {
            locale_holder->set_numeric(*bcp47_handle);
          } else if (strncmp(bcp47_key, "ca", 2) == 0) {
            locale_holder->set_calendar(*bcp47_handle);
          } else if (strncmp(bcp47_key, "kf", 2) == 0) {
            locale_holder->set_case_first(*bcp47_handle);
          } else if (strncmp(bcp47_key, "co", 2) == 0) {
            locale_holder->set_collation(*bcp47_handle);
          } else if (strncmp(bcp47_key, "hc", 2) == 0) {
            locale_holder->set_hour_cycle(*bcp47_handle);
          } else if (strncmp(bcp47_key, "nu", 2) == 0) {
            locale_holder->set_numbering_system(*bcp47_handle);
          }
      }
    }
  }

  uenum_close(keywords);

  return true;
}
}  // namespace

MaybeHandle<JSLocale> JSLocale::InitializeLocale(Isolate* isolate,
                                                 Handle<JSLocale> locale_holder,
                                                 Handle<String> locale,
                                                 Handle<JSReceiver> options) {
  static const char* const kMethod = "Intl.Locale";
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  UErrorCode status = U_ZERO_ERROR;

  // Get ICU locale format, and canonicalize it.
  char icu_result[ULOC_FULLNAME_CAPACITY];
  char icu_canonical[ULOC_FULLNAME_CAPACITY];

  if (locale->length() == 0) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kLocaleNotEmpty),
                    JSLocale);
  }

  v8::String::Utf8Value bcp47_locale(v8_isolate, v8::Utils::ToLocal(locale));
  CHECK_LT(0, bcp47_locale.length());
  CHECK_NOT_NULL(*bcp47_locale);

  int icu_length = uloc_forLanguageTag(
      *bcp47_locale, icu_result, ULOC_FULLNAME_CAPACITY, nullptr, &status);

  if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING ||
      icu_length == 0) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kLocaleBadParameters,
                      isolate->factory()->NewStringFromAsciiChecked(kMethod),
                      locale_holder),
        JSLocale);
    return MaybeHandle<JSLocale>();
  }

  Maybe<bool> error = InsertOptionsIntoLocale(isolate, options, icu_result);
  MAYBE_RETURN(error, MaybeHandle<JSLocale>());
  if (!error.FromJust()) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kLocaleBadParameters,
                      isolate->factory()->NewStringFromAsciiChecked(kMethod),
                      locale_holder),
        JSLocale);
    return MaybeHandle<JSLocale>();
  }
  DCHECK(error.FromJust());

  uloc_canonicalize(icu_result, icu_canonical, ULOC_FULLNAME_CAPACITY, &status);
  if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kLocaleBadParameters,
                      isolate->factory()->NewStringFromAsciiChecked(kMethod),
                      locale_holder),
        JSLocale);
    return MaybeHandle<JSLocale>();
  }

  if (!PopulateLocaleWithUnicodeTags(isolate, icu_canonical, locale_holder)) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kLocaleBadParameters,
                      isolate->factory()->NewStringFromAsciiChecked(kMethod),
                      locale_holder),
        JSLocale);
    return MaybeHandle<JSLocale>();
  }

  // Extract language, script and region parts.
  char icu_language[ULOC_LANG_CAPACITY];
  uloc_getLanguage(icu_canonical, icu_language, ULOC_LANG_CAPACITY, &status);

  char icu_script[ULOC_SCRIPT_CAPACITY];
  uloc_getScript(icu_canonical, icu_script, ULOC_SCRIPT_CAPACITY, &status);

  char icu_region[ULOC_COUNTRY_CAPACITY];
  uloc_getCountry(icu_canonical, icu_region, ULOC_COUNTRY_CAPACITY, &status);

  if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kLocaleBadParameters,
                      isolate->factory()->NewStringFromAsciiChecked(kMethod),
                      locale_holder),
        JSLocale);
    return MaybeHandle<JSLocale>();
  }

  Factory* factory = isolate->factory();

  // NOTE: One shouldn't use temporary handles, because they can go out of
  // scope and be garbage collected before properly assigned.
  // DON'T DO THIS: locale_holder->set_language(*f->NewStringAscii...);
  Handle<String> language = factory->NewStringFromAsciiChecked(icu_language);
  locale_holder->set_language(*language);

  if (strlen(icu_script) != 0) {
    Handle<String> script = factory->NewStringFromAsciiChecked(icu_script);
    locale_holder->set_script(*script);
  }

  if (strlen(icu_region) != 0) {
    Handle<String> region = factory->NewStringFromAsciiChecked(icu_region);
    locale_holder->set_region(*region);
  }

  char icu_base_name[ULOC_FULLNAME_CAPACITY];
  uloc_getBaseName(icu_canonical, icu_base_name, ULOC_FULLNAME_CAPACITY,
                   &status);
  // We need to convert it back to BCP47.
  char bcp47_result[ULOC_FULLNAME_CAPACITY];
  uloc_toLanguageTag(icu_base_name, bcp47_result, ULOC_FULLNAME_CAPACITY, true,
                     &status);
  if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kLocaleBadParameters,
                      isolate->factory()->NewStringFromAsciiChecked(kMethod),
                      locale_holder),
        JSLocale);
    return MaybeHandle<JSLocale>();
  }
  Handle<String> base_name = factory->NewStringFromAsciiChecked(bcp47_result);
  locale_holder->set_base_name(*base_name);

  // Produce final representation of the locale string, for toString().
  uloc_toLanguageTag(icu_canonical, bcp47_result, ULOC_FULLNAME_CAPACITY, true,
                     &status);
  if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kLocaleBadParameters,
                      isolate->factory()->NewStringFromAsciiChecked(kMethod),
                      locale_holder),
        JSLocale);
    return MaybeHandle<JSLocale>();
  }
  Handle<String> locale_handle =
      factory->NewStringFromAsciiChecked(bcp47_result);
  locale_holder->set_locale(*locale_handle);

  return locale_holder;
}

namespace {

Handle<String> MorphLocale(Isolate* isolate, String* input,
                           int32_t (*morph_func)(const char*, char*, int32_t,
                                                 UErrorCode*)) {
  Factory* factory = isolate->factory();
  char localeBuffer[ULOC_FULLNAME_CAPACITY];
  UErrorCode status = U_ZERO_ERROR;
  DCHECK_NOT_NULL(morph_func);
  int32_t length = (*morph_func)(input->ToCString().get(), localeBuffer,
                                 ULOC_FULLNAME_CAPACITY, &status);
  DCHECK(U_SUCCESS(status));
  DCHECK_GT(length, 0);
  std::string locale(localeBuffer, length);
  std::replace(locale.begin(), locale.end(), '_', '-');
  return factory->NewStringFromAsciiChecked(locale.c_str());
}

}  // namespace

Handle<String> JSLocale::Maximize(Isolate* isolate, String* locale) {
  return MorphLocale(isolate, locale, uloc_addLikelySubtags);
}

Handle<String> JSLocale::Minimize(Isolate* isolate, String* locale) {
  return MorphLocale(isolate, locale, uloc_minimizeSubtags);
}

}  // namespace internal
}  // namespace v8
