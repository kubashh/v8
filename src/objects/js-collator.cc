// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT
#include "src/objects/js-collator.h"

#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects/js-collator-inl.h"
#include "unicode/coll.h"
#include "unicode/locid.h"
#include "unicode/strenum.h"
#include "unicode/ucol.h"
#include "unicode/uloc.h"

namespace v8 {
namespace internal {

namespace {

enum class Usage { SORT, SEARCH };

// TODO(gsathya): Consider internalizing the value strings.
void CreateDataPropertyForOptions(Isolate* isolate, Handle<JSObject> options,
                                  Handle<String> key, const char* value) {
  CHECK_NOT_NULL(value);
  Handle<String> value_str =
      isolate->factory()->NewStringFromAsciiChecked(value);

  // This is a brand new JSObject that shouldn't already have the same
  // key so this shouldn't fail.
  CHECK(JSReceiver::CreateDataProperty(isolate, options, key, value_str,
                                       kDontThrow)
            .FromJust());
}

void CreateDataPropertyForOptions(Isolate* isolate, Handle<JSObject> options,
                                  Handle<String> key, bool value) {
  Handle<Object> value_obj = isolate->factory()->ToBoolean(value);

  // This is a brand new JSObject that shouldn't already have the same
  // key so this shouldn't fail.
  CHECK(JSReceiver::CreateDataProperty(isolate, options, key, value_obj,
                                       kDontThrow)
            .FromJust());
}

}  // anonymous namespace

// static
Handle<JSObject> JSCollator::ResolvedOptions(Isolate* isolate,
                                             Handle<JSCollator> collator) {
  Handle<JSObject> options =
      isolate->factory()->NewJSObject(isolate->object_function());

  icu::Collator* icu_collator = collator->icu_collator()->raw();
  CHECK_NOT_NULL(icu_collator);

  UErrorCode status = U_ZERO_ERROR;
  bool numeric =
      icu_collator->getAttribute(UCOL_NUMERIC_COLLATION, status) == UCOL_ON;
  CHECK(U_SUCCESS(status));
  CreateDataPropertyForOptions(isolate, options,
                               isolate->factory()->numeric_string(), numeric);

  const char* case_first = nullptr;
  status = U_ZERO_ERROR;
  switch (icu_collator->getAttribute(UCOL_CASE_FIRST, status)) {
    case UCOL_LOWER_FIRST:
      case_first = "lower";
      break;
    case UCOL_UPPER_FIRST:
      case_first = "upper";
      break;
    default:
      case_first = "false";
  }
  CHECK(U_SUCCESS(status));
  CreateDataPropertyForOptions(
      isolate, options, isolate->factory()->caseFirst_string(), case_first);

  const char* sensitivity = nullptr;
  status = U_ZERO_ERROR;
  switch (icu_collator->getAttribute(UCOL_STRENGTH, status)) {
    case UCOL_PRIMARY: {
      CHECK(U_SUCCESS(status));
      status = U_ZERO_ERROR;
      // case level: true + s1 -> case, s1 -> base.
      if (UCOL_ON == icu_collator->getAttribute(UCOL_CASE_LEVEL, status)) {
        sensitivity = "case";
      } else {
        sensitivity = "base";
      }
      CHECK(U_SUCCESS(status));
      break;
    }
    case UCOL_SECONDARY:
      sensitivity = "accent";
      break;
    case UCOL_TERTIARY:
      sensitivity = "variant";
      break;
    case UCOL_QUATERNARY:
      // We shouldn't get quaternary and identical from ICU, but if we do
      // put them into variant.
      sensitivity = "variant";
      break;
    default:
      sensitivity = "variant";
  }
  CHECK(U_SUCCESS(status));
  CreateDataPropertyForOptions(
      isolate, options, isolate->factory()->sensitivity_string(), sensitivity);

  status = U_ZERO_ERROR;
  bool ignore_punctuation = icu_collator->getAttribute(UCOL_ALTERNATE_HANDLING,
                                                       status) == UCOL_SHIFTED;
  CHECK(U_SUCCESS(status));
  CreateDataPropertyForOptions(isolate, options,
                               isolate->factory()->ignorePunctuation_string(),
                               ignore_punctuation);

  status = U_ZERO_ERROR;
  icu::Locale icu_locale = icu_collator->getLocale(ULOC_VALID_LOCALE, status);
  CHECK(U_SUCCESS(status));

  const char* collation = "default";
  const char* usage = "sort";
  const char* bcp47_key = "co";
  // Convert bcp47 key to legacy keytype for icu::Locale::getKeywordValue.
  const char* legacy_key = uloc_toLegacyKey(bcp47_key);
  CHECK_NOT_NULL(legacy_key);

  status = U_ZERO_ERROR;
  char legacy_value[ULOC_FULLNAME_CAPACITY];
  icu_locale.getKeywordValue(legacy_key, legacy_value, ULOC_FULLNAME_CAPACITY,
                             status);
  if (U_SUCCESS(status)) {
    CHECK_NOT_NULL(legacy_value);

    const char* bcp47_value = uloc_toUnicodeLocaleType(bcp47_key, legacy_value);
    // This is working around a weirdness in ICU, instead of returning
    // a failure status for a missing value, ICU returns garbage. This
    // turns into a nullptr when passed to uloc_toUnicodeLocaleType.
    if (bcp47_value != nullptr) {
      if (strcmp(bcp47_value, "search") == 0) {
        usage = "search";

        // Search is disallowed as a collation value per spec. Let's
        // use `default`, instead.
        //
        // https://tc39.github.io/ecma402/#sec-properties-of-intl-collator-instances
        collation = "default";
      } else {
        collation = bcp47_value;
      }
    }
  }
  CreateDataPropertyForOptions(
      isolate, options, isolate->factory()->collation_string(), collation);

  CreateDataPropertyForOptions(isolate, options,
                               isolate->factory()->usage_string(), usage);

  // In case, usage is set to search, as per the spec, V8 shouldn't
  // add the 'co-search' unicode extension to the language tag. But,
  // deleting it from the language tag returned by ICU is expensive as
  // we have to reparse the tag and trim it, or create a new ICU
  // locale class without the 'co' unicode extension.
  //
  // Since V8 will anyway not consider the 'co-search' value if passed
  // in as an extension, I think it's fine to diverge from the spec
  // here. For example:
  //   let c = new Intl.Collator('en-US', { usage: search });
  //   c.resolvedOptions().locale // 'en-US-u-co-search'
  //
  // But instead, the correct result is 'en-US'.
  char result[ULOC_FULLNAME_CAPACITY];
  status = U_ZERO_ERROR;
  uloc_toLanguageTag(icu_locale.getName(), result, ULOC_FULLNAME_CAPACITY,
                     FALSE, &status);
  CHECK(U_SUCCESS(status));

  CreateDataPropertyForOptions(isolate, options,
                               isolate->factory()->locale_string(), result);
  return options;
}

namespace {

std::map<std::string, std::string> LookupUnicodeExtensions(
    char* locale_id, std::set<std::string>& relevant_keys) {
  std::map<std::string, std::string> extensions;

  UErrorCode status = U_ZERO_ERROR;
  UEnumeration* keywords = uloc_openKeywords(locale_id, &status);
  if (U_FAILURE(status)) return extensions;

  if (!keywords) return extensions;
  char value[ULOC_FULLNAME_CAPACITY];

  int32_t length;
  status = U_ZERO_ERROR;
  for (const char* keyword = uenum_next(keywords, &length, &status);
       keyword != nullptr; keyword = uenum_next(keywords, &length, &status)) {
    // Ignore failures in ICU and skip to the next keyword.
    //
    // This is fine.™
    if (U_FAILURE(status)) {
      status = U_ZERO_ERROR;
      continue;
    }

    uloc_getKeywordValue(locale_id, keyword, value, ULOC_FULLNAME_CAPACITY,
                         &status);

    // Ignore failures in ICU and skip to the next keyword.
    //
    // This is fine.™
    if (U_FAILURE(status)) {
      status = U_ZERO_ERROR;
      continue;
    }

    const char* bcp47_key = uloc_toUnicodeLocaleKey(keyword);
    // Ignore keywords that we don't recognize - spec allows that.
    if (bcp47_key && (relevant_keys.find(bcp47_key) != relevant_keys.end())) {
      const char* bcp47_value = uloc_toUnicodeLocaleType(bcp47_key, value);
      extensions.insert(
          std::pair<std::string, std::string>(bcp47_key, bcp47_value));
    }
  }

  uenum_close(keywords);
  return extensions;
}

void SetCaseFirstOption(icu::Collator* icu_collator, const char* value) {
  CHECK_NOT_NULL(icu_collator);
  CHECK_NOT_NULL(value);
  UErrorCode status = U_ZERO_ERROR;
  if (strcmp(value, "upper") == 0) {
    icu_collator->setAttribute(UCOL_CASE_FIRST, UCOL_UPPER_FIRST, status);
  } else if (strcmp(value, "lower") == 0) {
    icu_collator->setAttribute(UCOL_CASE_FIRST, UCOL_LOWER_FIRST, status);
  } else {
    icu_collator->setAttribute(UCOL_CASE_FIRST, UCOL_OFF, status);
  }
  CHECK(U_SUCCESS(status));
}

}  // anonymous namespace

// static
MaybeHandle<JSCollator> JSCollator::InitializeCollator(
    Isolate* isolate, Handle<JSCollator> collator, Handle<Object> locales,
    Handle<Object> options_obj) {
  // 1. Let requestedLocales be ? CanonicalizeLocaleList(locales).
  Handle<JSObject> requested_locales;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, requested_locales,
                             Intl::CanonicalizeLocaleListJS(isolate, locales),
                             JSCollator);

  // 2. If options is undefined, then
  if (options_obj->IsUndefined(isolate)) {
    // 2. a. Let options be ObjectCreate(null).
    options_obj = isolate->factory()->NewJSObjectWithNullProto();
  } else {
    // 3. Else
    // 3. a. Let options be ? ToObject(options).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, options_obj,
        Object::ToObject(isolate, options_obj, "Intl.Collator"), JSCollator);
  }

  // At this point, options_obj can either be a JSObject or a JSProxy only.
  Handle<JSReceiver> options = Handle<JSReceiver>::cast(options_obj);

  // 4. Let usage be ? GetOption(options, "usage", "string", « "sort",
  // "search" », "sort").
  std::vector<const char*> values = {"sort", "search"};
  std::unique_ptr<char[]> usage_str = nullptr;
  Maybe<bool> found_usage = Intl::GetStringOption(
      isolate, options, "usage", values, "Intl.Collator", &usage_str);
  MAYBE_RETURN(found_usage, MaybeHandle<JSCollator>());

  Usage usage = Usage::SORT;
  if (found_usage.FromJust()) {
    DCHECK_NOT_NULL(usage_str.get());
    if (strcmp(usage_str.get(), "search") == 0) usage = Usage::SEARCH;
  }

  // TODO(gsathya): This is currently done as part of the
  // Intl::ResolveLocale call below. Fix this once resolveLocale is
  // changed to not do the lookup.
  //
  // 9. Let matcher be ? GetOption(options, "localeMatcher", "string",
  // « "lookup", "best fit" », "best fit").
  // 10. Set opt.[[localeMatcher]] to matcher.

  // 11. Let numeric be ? GetOption(options, "numeric", "boolean",
  // undefined, undefined).
  // 12. If numeric is not undefined, then
  //    a. Let numeric be ! ToString(numeric).
  //
  // Note: We omit the ToString(numeric) operation as it's not
  // observable. Intl::GetBoolOption returns a Boolean and
  // ToString(Boolean) is not side-effecting.
  //
  // 13. Set opt.[[kn]] to numeric.
  bool numeric;
  Maybe<bool> found_numeric = Intl::GetBoolOption(isolate, options, "numeric",
                                                  "Intl.Collator", &numeric);
  MAYBE_RETURN(found_numeric, MaybeHandle<JSCollator>());

  // 14. Let caseFirst be ? GetOption(options, "caseFirst", "string",
  //     « "upper", "lower", "false" », undefined).
  // 15. Set opt.[[kf]] to caseFirst.
  values = {"upper", "lower", "false"};
  std::unique_ptr<char[]> case_first_str = nullptr;
  Maybe<bool> found_case_first = Intl::GetStringOption(
      isolate, options, "caseFirst", values, "Intl.Collator", &case_first_str);
  MAYBE_RETURN(found_case_first, MaybeHandle<JSCollator>());

  // The relevant unicode extensions accepted by Collator as specified here:
  // https://tc39.github.io/ecma402/#sec-intl-collator-internal-slots
  //
  // 16. Let relevantExtensionKeys be %Collator%.[[RelevantExtensionKeys]].
  std::set<std::string> relevant_extension_keys{"co", "kn", "kf"};

  // We don't pass the relevant_extension_keys to ResolveLocale here
  // as per the spec.
  //
  // In ResolveLocale, the spec makes sure we only pick and use the
  // relevant extension keys and ignore any other keys. Also, in
  // ResolveLocale, the spec makes sure that if a given key has both a
  // value in the options object and an unicode extension value, then
  // we pick the value provided in the options object.
  // For example: in the case of `new Intl.Collator('en-u-kn-true', {
  // numeric: false })` the value `false` is used for the `numeric`
  // key.
  //
  // Instead of performing all this validation in ResolveLocale, we
  // just perform it inline below. In the future when we port
  // ResolveLocale to C++, we can make all these validations generic
  // and move it ResolveLocale.
  //
  // 17. Let r be ResolveLocale(%Collator%.[[AvailableLocales]],
  // requestedLocales, opt, %Collator%.[[RelevantExtensionKeys]],
  // localeData).
  // 18. Set collator.[[Locale]] to r.[[locale]].
  Handle<JSObject> r;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, r,
      Intl::ResolveLocale(isolate, "collator", requested_locales, options),
      JSCollator);

  Handle<String> locale_with_extension_key =
      isolate->factory()->NewStringFromStaticChars("localeWithExtension");
  Handle<Object> locale_with_extension_obj =
      JSObject::GetDataProperty(r, locale_with_extension_key);

  // The locale_with_extension has to be a string. Either a user
  // provided canonicalized string or the default locale.
  CHECK(locale_with_extension_obj->IsString());
  Handle<String> locale_with_extension =
      Handle<String>::cast(locale_with_extension_obj);

  std::unique_ptr<char[]> locale_with_extension_cstr =
      locale_with_extension->ToCString();
  CHECK_NOT_NULL(locale_with_extension_cstr.get());

  UErrorCode status = U_ZERO_ERROR;
  char locale_id[ULOC_FULLNAME_CAPACITY];
  int length = 0;

  // bcp47_locale_str should be a canonicalized language tag, which
  // means this shouldn't fail.
  uloc_forLanguageTag(locale_with_extension_cstr.get(), locale_id,
                      ULOC_FULLNAME_CAPACITY, &length, &status);
  CHECK(U_SUCCESS(status));
  CHECK_LT(0, length);

  // As per,
  // https://tc39.github.io/ecma402/#sec-unicode-locale-extension-sequences
  //
  // Private use subtags should be not used as an unicode locale
  // extension sequences.
  const char* private_use_key = uloc_toLegacyKey("x");
  status = U_ZERO_ERROR;
  uloc_setKeywordValue(private_use_key, NULL, locale_id, ULOC_FULLNAME_CAPACITY,
                       &status);
  CHECK(U_SUCCESS(status));

  std::map<std::string, std::string> extensions =
      LookupUnicodeExtensions(locale_id, relevant_extension_keys);

  // 19. Let collation be r.[[co]].
  //
  // r.[[co]] is already set as part of the icu::Locale creation as
  // icu parses unicode extensions and sets the keywords.
  //
  // We need to sanitize the keywords based on certain ECMAScript rules.
  //
  // As per https://tc39.github.io/ecma402/#sec-intl-collator-internal-slots:
  // The values "standard" and "search" must not be used as elements
  // in any [[SortLocaleData]][locale].co and
  // [[SearchLocaleData]][locale].co list.
  if (extensions.find("co") != extensions.end()) {
    std::string value = extensions.at("co");
    if ((value == "search") || (value == "standard")) {
      UErrorCode status = U_ZERO_ERROR;
      const char* key = uloc_toLegacyKey("co");
      DCHECK_NOT_NULL(key);
      uloc_setKeywordValue(key, NULL, locale_id, ULOC_FULLNAME_CAPACITY,
                           &status);
      CHECK(U_SUCCESS(status));
    }
  }

  // 5. Set collator.[[Usage]] to usage.
  // 6. If usage is "sort", then
  //    a. Let localeData be %Collator%.[[SortLocaleData]].
  // 7. Else,
  //    a. Let localeData be %Collator%.[[SearchLocaleData]].
  //
  // The Intl spec doesn't allow us to "search" as an extension value
  // for collation as previously seen. But the only way to pass the
  // value "search" for collation from the options object to ICU is to
  // use the 'co' extension keyword.
  //
  // This will need to be filtered out when creating the
  // resolvedOptions object.
  if (usage == Usage::SEARCH) {
    const char* key = uloc_toLegacyKey("co");
    CHECK_NOT_NULL(key);
    const char* value = uloc_toLegacyType(key, "search");
    CHECK_NOT_NULL(value);
    UErrorCode status = U_ZERO_ERROR;
    uloc_setKeywordValue(key, value, locale_id, ULOC_FULLNAME_CAPACITY,
                         &status);
    CHECK(U_SUCCESS(status));
  }

  // 20. If collation is null, let collation be "default".
  // 21. Set collator.[[Collation]] to collation.
  //
  // We don't store the collation value as per the above two steps
  // here. The collation value can be looked up from icu::Collator on
  // demand, as part of Intl.Collator.prototype.resolvedOptions.

  icu::Locale icu_locale(locale_id);
  if (icu_locale.isBogus()) {
    FATAL("Failed to create ICU locale, are ICU data files missing?");
  }

  status = U_ZERO_ERROR;
  std::unique_ptr<icu::Collator> icu_collator(
      icu::Collator::createInstance(icu_locale, status));
  if (U_FAILURE(status) || icu_collator.get() == nullptr) {
    status = U_ZERO_ERROR;
    // Remove extensions and try again.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    icu_collator.reset(
        icu::Collator::createInstance(no_extension_locale, status));

    if (U_FAILURE(status) || icu_collator.get() == nullptr) {
      FATAL("Failed to create ICU collator, are ICU data files missing?");
    }
  }
  DCHECK(U_SUCCESS(status));
  CHECK_NOT_NULL(icu_collator.get());

  // 22. If relevantExtensionKeys contains "kn", then
  //     a. Set collator.[[Numeric]] to ! SameValue(r.[[kn]], "true").
  //
  // If the numeric value is passed in through the options object,
  // then we use it. Otherwise, we check if the numeric value is
  // passed in through the unicode extensions.
  status = U_ZERO_ERROR;
  if (found_numeric.FromJust()) {
    icu_collator->setAttribute(UCOL_NUMERIC_COLLATION,
                               numeric ? UCOL_ON : UCOL_OFF, status);
    CHECK(U_SUCCESS(status));
  } else if (extensions.find("kn") != extensions.end()) {
    std::string value = extensions.at("kn");

    numeric = value == "true";

    icu_collator->setAttribute(UCOL_NUMERIC_COLLATION,
                               numeric ? UCOL_ON : UCOL_OFF, status);
    CHECK(U_SUCCESS(status));
  }

  // 23. If relevantExtensionKeys contains "kf", then
  //     a. Set collator.[[CaseFirst]] to r.[[kf]].
  //
  // If the caseFirst value is passed in through the options object,
  // then we use it. Otherwise, we check if the caseFirst value is
  // passed in through the unicode extensions.
  if (found_case_first.FromJust()) {
    const char* case_first_cstr = case_first_str.get();
    SetCaseFirstOption(icu_collator.get(), case_first_cstr);
  } else if (extensions.find("kf") != extensions.end()) {
    std::string value = extensions.at("kf");
    SetCaseFirstOption(icu_collator.get(), value.c_str());
  }

  // Normalization is always on, by the spec. We are free to optimize
  // if the strings are already normalized (but we don't have a way to tell
  // that right now).
  status = U_ZERO_ERROR;
  icu_collator->setAttribute(UCOL_NORMALIZATION_MODE, UCOL_ON, status);
  CHECK(U_SUCCESS(status));

  // 24. Let sensitivity be ? GetOption(options, "sensitivity",
  // "string", « "base", "accent", "case", "variant" », undefined).
  values = {"base", "accent", "case", "variant"};
  std::unique_ptr<char[]> sensitivity_str = nullptr;
  Maybe<bool> found_sensitivity =
      Intl::GetStringOption(isolate, options, "sensitivity", values,
                            "Intl.Collator", &sensitivity_str);
  MAYBE_RETURN(found_sensitivity, MaybeHandle<JSCollator>());

  // 25. If sensitivity is undefined, then
  if (!found_sensitivity.FromJust()) {
    // 25. a. If usage is "sort", then
    if (usage == Usage::SORT) {
      // 25. a. i. Let sensitivity be "variant".
      // 26. Set collator.[[Sensitivity]] to sensitivity.
      icu_collator->setStrength(icu::Collator::TERTIARY);
    }
  } else {
    DCHECK(found_sensitivity.FromJust());
    const char* sensitivity_cstr = sensitivity_str.get();
    DCHECK_NOT_NULL(sensitivity_cstr);

    // 26. Set collator.[[Sensitivity]] to sensitivity.
    if (strcmp(sensitivity_cstr, "base") == 0) {
      icu_collator->setStrength(icu::Collator::PRIMARY);
    } else if (strcmp(sensitivity_cstr, "accent") == 0) {
      icu_collator->setStrength(icu::Collator::SECONDARY);
    } else if (strcmp(sensitivity_cstr, "case") == 0) {
      icu_collator->setStrength(icu::Collator::PRIMARY);
      status = U_ZERO_ERROR;
      icu_collator->setAttribute(UCOL_CASE_LEVEL, UCOL_ON, status);
      CHECK(U_SUCCESS(status));
    } else {
      DCHECK_EQ(0, strcmp(sensitivity_cstr, "variant"));
      icu_collator->setStrength(icu::Collator::TERTIARY);
    }
  }

  // 27.Let ignorePunctuation be ? GetOption(options,
  // "ignorePunctuation", "boolean", undefined, false).
  bool ignore_punctuation;
  Maybe<bool> found_ignore_punctuation =
      Intl::GetBoolOption(isolate, options, "ignorePunctuation",
                          "Intl.Collator", &ignore_punctuation);
  MAYBE_RETURN(found_ignore_punctuation, MaybeHandle<JSCollator>());

  // 28. Set collator.[[IgnorePunctuation]] to ignorePunctuation.
  if (found_ignore_punctuation.FromJust() && ignore_punctuation) {
    status = U_ZERO_ERROR;
    icu_collator->setAttribute(UCOL_ALTERNATE_HANDLING, UCOL_SHIFTED, status);
    CHECK(U_SUCCESS(status));
  }

  Handle<Managed<icu::Collator>> managed_collator =
      Managed<icu::Collator>::FromUniquePtr(isolate, 0,
                                            std::move(icu_collator));
  collator->set_icu_collator(*managed_collator);

  // 29. Return collator.
  return collator;
}

}  // namespace internal
}  // namespace v8
