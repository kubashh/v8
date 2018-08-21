// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-date-time-format.h"

#include <string>

#include "src/heap/factory.h"
#include "src/isolate.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-date-time-format-inl.h"

#include "unicode/calendar.h"
#include "unicode/smpdtfmt.h"
#include "unicode/unistr.h"

namespace v8 {
namespace internal {

namespace {
struct PatternMap {
  const char* const pattern;
  const char* const value;
};
struct PatternItem {
  const char* const property;
  // It is important for the pattern in the pairs from longer one to shorter one
  // if the longer one contains substring of an shorter one.
  std::vector<const PatternMap> pairs;
};

std::vector<const PatternItem> kPatternItems = {
    {"weekday", {{"EEEEE", "narrow"}, {"EEEE", "long"}, {"EEE", "short"}}},
    {"era", {{"GGGGG", "narrow"}, {"GGGG", "long"}, {"GGG", "short"}}},
    {"year", {{"yy", "2-digit"}, {"y", "numeric"}}},
    // Sometimes we get L instead of M for month - standalone name.
    {"month",
     {{"MMMMM", "narrow"},
      {"MMMM", "long"},
      {"MMM", "short"},
      {"MM", "2-digit"},
      {"M", "numeric"},
      {"LLLLL", "narrow"},
      {"LLLL", "long"},
      {"LLL", "short"},
      {"LL", "2-digit"},
      {"L", "numeric"}}},
    {"day", {{"dd", "2-digit"}, {"d", "numeric"}}},
    {"hour",
     {{"HH", "2-digit"},
      {"H", "numeric"},
      {"hh", "2-digit"},
      {"h", "numeric"}}},
    {"minute", {{"mm", "2-digit"}, {"m", "numeric"}}},
    {"second", {{"ss", "2-digit"}, {"s", "numeric"}}},
    {"timeZoneName", {{"zzzz", "long"}, {"z", "short"}}}};

void SetPropertyFromPattern(Isolate* isolate, const std::string& pattern,
                            Handle<JSObject> options) {
  Factory* factory = isolate->factory();
  for (auto item = kPatternItems.cbegin(); item != kPatternItems.cend();
       ++item) {
    for (auto pair = item->pairs.cbegin(); pair != item->pairs.cend(); ++pair) {
      if (pattern.find(pair->pattern) != std::string::npos) {
        // After we find the first pair in the item which matching the pattern,
        // we set the property and look for the next item in kPatternItems.
        CHECK(JSReceiver::CreateDataProperty(
                  isolate, options,
                  factory->NewStringFromAsciiChecked(item->property),
                  factory->NewStringFromAsciiChecked(pair->value), kDontThrow)
                  .FromJust());
        break;
      }
    }
  }
  // hour12
  // b. If p is "hour12", then
  //  i. Let hc be dtf.[[HourCycle]].
  //  ii. If hc is "h11" or "h12", let v be true.
  //  iii. Else if, hc is "h23" or "h24", let v be false.
  //  iv. Else, let v be undefined.
  if (pattern.find("h") != std::string::npos) {
    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->NewStringFromStaticChars("hour12"),
              factory->true_value(), kDontThrow)
              .FromJust());
  } else if (pattern.find("H") != std::string::npos) {
    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->NewStringFromStaticChars("hour12"),
              factory->false_value(), kDontThrow)
              .FromJust());
  }
}

}  // namespace

MaybeHandle<JSObject> JSDateTimeFormat::ResolvedOptions(
    Isolate* isolate, Handle<JSReceiver> format_holder) {
  Factory* factory = isolate->factory();

  // 3. Let dtf be ? UnwrapDateTimeFormat(dtf).
  if (!Intl::IsObjectOfType(isolate, format_holder,
                            Intl::Type::kDateTimeFormat)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                                 factory->NewStringFromStaticChars(
                                     "Intl.DateTimeFormat.resolvedOptions"),
                                 format_holder),
                    JSObject);
  }
  CHECK(format_holder->IsJSObject());
  icu::SimpleDateFormat* icu_simple_date_format =
      DateFormat::UnpackDateFormat(Handle<JSObject>::cast(format_holder));

  // 4. Let options be ! ObjectCreate(%ObjectPrototype%).
  Handle<JSObject> options = factory->NewJSObject(isolate->object_function());

  // 5. For each row of Table 6, except the header row, in any order, do
  // a. Let p be the Property value of the current row.
  Handle<Object> resolved_obj;

  // After we move all the data to JSDateTimeFormat, we should just get locale
  // and numberingSystem from the member data. This is here until we move
  // everything.
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, resolved_obj,
      JSReceiver::GetProperty(isolate, format_holder,
                              factory->intl_resolved_symbol()),
      JSObject);
  CHECK(resolved_obj->IsJSObject());
  Handle<JSObject> resolved = Handle<JSObject>::cast(resolved_obj);

  // locale
  Handle<Object> locale_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, locale_obj,
      JSReceiver::GetProperty(isolate, resolved, factory->locale_string()),
      JSObject);
  CHECK(locale_obj->IsString());
  Handle<String> locale = Handle<String>::cast(locale_obj);
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->locale_string(), locale, kDontThrow)
            .FromJust());

  // numberingSystem
  // replace to factory->numberingSystem_string(), after +/1168518 landed.
  Handle<String> numberingSystem_string =
      factory->NewStringFromStaticChars("numberingSystem");
  Handle<Object> numbering_system_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, numbering_system_obj,
      JSReceiver::GetProperty(isolate, resolved, numberingSystem_string),
      JSObject);
  if (numbering_system_obj->IsString()) {
    Handle<String> numbering_system =
        Handle<String>::cast(numbering_system_obj);
    CHECK(JSReceiver::CreateDataProperty(isolate, options,
                                         numberingSystem_string,
                                         numbering_system, kDontThrow)
              .FromJust());
  }

  icu::UnicodeString pattern_unicode;
  icu_simple_date_format->toPattern(pattern_unicode);
  std::string pattern;
  pattern_unicode.toUTF8String(pattern);
  SetPropertyFromPattern(isolate, pattern, options);

  // calendar
  const icu::Calendar* calendar = icu_simple_date_format->getCalendar();
  // getType() returns legacy calendar type name instead of LDML/BCP47 calendar
  // key values. intl.js maps them to BCP47 values for key "ca".
  // TODO(jshin): Consider doing it here, instead.
  std::string calendar_str = calendar->getType();

  // Maps ICU calendar names to LDML/BCP47 types for key 'ca'.
  // See typeMap section in third_party/icu/source/data/misc/keyTypeData.txt
  // and
  // http://www.unicode.org/repos/cldr/tags/latest/common/bcp47/calendar.xml
  if (calendar_str == "gregorian") {
    calendar_str = "gregory";
  } else if (calendar_str == "ethiopic-amete-alem") {
    calendar_str = "ethioaa";
  }
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->NewStringFromStaticChars("calendar"),
            factory->NewStringFromAsciiChecked(calendar_str.c_str()),
            kDontThrow)
            .FromJust());

  // timezone
  const icu::TimeZone& tz = calendar->getTimeZone();
  icu::UnicodeString time_zone;
  tz.getID(time_zone);
  UErrorCode error = U_ZERO_ERROR;
  icu::UnicodeString canonical_time_zone;
  icu::TimeZone::getCanonicalID(time_zone, canonical_time_zone, error);
  if (U_SUCCESS(error)) {
    Handle<String> timezone_value;
    // In CLDR (http://unicode.org/cldr/trac/ticket/9943), Etc/UTC is made
    // a separate timezone ID from Etc/GMT even though they're still the same
    // timezone. We have Etc/UTC because 'UTC', 'Etc/Universal',
    // 'Etc/Zulu' and others are turned to 'Etc/UTC' by ICU. Etc/GMT comes
    // from Etc/GMT0, Etc/GMT+0, Etc/GMT-0, Etc/Greenwich.
    // ecma402##sec-canonicalizetimezonename step 3
    if (canonical_time_zone == UNICODE_STRING_SIMPLE("Etc/UTC") ||
        canonical_time_zone == UNICODE_STRING_SIMPLE("Etc/GMT")) {
      timezone_value = factory->NewStringFromAsciiChecked("UTC");
    } else {
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, timezone_value,
          factory->NewStringFromTwoByte(
              Vector<const uint16_t>(reinterpret_cast<const uint16_t*>(
                                         canonical_time_zone.getBuffer()),
                                     canonical_time_zone.length())),
          JSObject);
    }
    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->NewStringFromStaticChars("timeZone"),
              timezone_value, kDontThrow)
              .FromJust());
  } else {
    // Somehow on Window we will reach here.
    CHECK(JSReceiver::CreateDataProperty(
              isolate, options, factory->NewStringFromStaticChars("timeZone"),
              factory->undefined_value(), kDontThrow)
              .FromJust());
  }
  return options;
}

}  // namespace internal
}  // namespace v8
