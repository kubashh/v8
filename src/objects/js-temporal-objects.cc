// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-temporal-objects.h"

#include <iomanip>
#include <set>
#include <sstream>

#include "src/common/globals.h"
#include "src/date/date.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/numbers/conversions-inl.h"
#ifdef V8_INTL_SUPPORT
#include "src/objects/intl-objects.h"
#include "src/objects/js-date-time-format.h"
#endif  // V8_INTL_SUPPORT
#include "src/objects/js-objects-inl.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-temporal-objects-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/option-utils.h"
#include "src/objects/property-descriptor.h"
#include "src/strings/string-builder-inl.h"

namespace v8 {
namespace internal {

// Abstract Operations in Temporal
namespace temporal {
/**
 * This header declare the Abstract Operations defined in the
 * Temporal spec with the enum and struct for them.
 */

// Struct
struct DateTimeRecordCommon {
  int32_t year;
  int32_t month;
  int32_t day;
  int32_t hour;
  int32_t minute;
  int32_t second;
  int32_t millisecond;
  int32_t microsecond;
  int32_t nanosecond;
};

struct DurationRecord {
  double years;
  double months;
  double weeks;
  double days;
  double hours;
  double minutes;
  double seconds;
  double milliseconds;
  double microseconds;
  double nanoseconds;
};

struct TimeZoneRecord {
  bool z;
  std::string offset_string;
  std::string name;
};

V8_WARN_UNUSED_RESULT Maybe<std::string> BuiltinTimeZoneGetOffsetStdStringFor(
    Isolate* isolate, Handle<JSReceiver> time_zone,
    Handle<JSTemporalInstant> instant);

// ISO8601 String Parsing

// #sec-temporal-parsetemporalcalendarstring
V8_WARN_UNUSED_RESULT Maybe<std::string> ParseTemporalCalendarString(
    Isolate* isolate, Handle<String> iso_string);

// #sec-temporal-parsetemporaltimezone
V8_WARN_UNUSED_RESULT Maybe<std::string> ParseTemporalTimeZone(
    Isolate* isolate, Handle<String> string);

V8_WARN_UNUSED_RESULT Maybe<int64_t> ParseTimeZoneOffsetString(
    Isolate* isolate, Handle<String> offset_string,
    bool throwIfNotSatisfy = true);

void BalanceISODate(Isolate* isolate, int32_t* year, int32_t* month,
                    int32_t* day);
// #sec-temporal-isvalidepochnanoseconds
bool IsValidEpochNanoseconds(Isolate* isolate,
                             Handle<BigInt> epoch_nanoseconds);

// #sec-temporal-isvalidduration
bool IsValidDuration(Isolate* isolate, const DurationRecord& dur);

// #sec-temporal-isodaysinmonth
int32_t ISODaysInMonth(Isolate* isolate, int32_t year, int32_t month);

// #sec-temporal-isodaysinyear
int32_t ISODaysInYear(Isolate* isolate, int32_t year);

bool IsValidTime(Isolate* isolate, int32_t hour, int32_t minute, int32_t second,
                 int32_t millisecond, int32_t microsecond, int32_t nanosecond);

// #sec-temporal-isvalidisodate
bool IsValidISODate(Isolate* isolate, int32_t year, int32_t month, int32_t day);

// #sec-temporal-balanceisoyearmonth
void BalanceISOYearMonth(Isolate* isolate, int32_t* year, int32_t* month);

// #sec-temporal-balancetime
V8_WARN_UNUSED_RESULT DateTimeRecordCommon
BalanceTime(Isolate* isolate, int64_t hour, int64_t minute, int64_t second,
            int64_t millisecond, int64_t microsecond, int64_t nanosecond);

// #sec-temporal-getoffsetnanosecondsfor
V8_WARN_UNUSED_RESULT Maybe<int64_t> GetOffsetNanosecondsFor(
    Isolate* isolate, Handle<JSReceiver> time_zone, Handle<Object> instant);

bool IsBuiltinCalendar(Isolate* isolate, Handle<String> id);

// #sec-temporal-getiso8601calendar
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalCalendar> GetISO8601Calendar(
    Isolate* isolate);

// #sec-isvalidtimezonename
bool IsValidTimeZoneName(Isolate* isolate, Handle<String> time_zone);
bool IsValidTimeZoneName(Isolate* isolate, const std::string& time_zone);

bool IsUTC(Isolate* isolate, Handle<String> time_zone);
bool IsUTC(Isolate* isolate, const std::string& time_zone);

// #sec-canonicalizetimezonename
V8_WARN_UNUSED_RESULT Handle<String> CanonicalizeTimeZoneName(
    Isolate* isolate, Handle<String> identifier);
V8_WARN_UNUSED_RESULT std::string CanonicalizeTimeZoneName(
    Isolate* isolate, const std::string& identifier);

inline double R(double d) { return static_cast<int64_t>(d); }

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)

#ifdef DEBUG
#define TEMPORAL_DEBUG_INFO AT
#define TEMPORAL_ENTER_FUNC
//#define TEMPORAL_ENTER_FUNC \
//  { printf("Start: %s\n", __func__); }
#else
// #define TEMPORAL_DEBUG_INFO ""
#define TEMPORAL_DEBUG_INFO AT
#define TEMPORAL_ENTER_FUNC
//#define TEMPORAL_ENTER_FUNC  { printf("Start: %s\n", __func__); }
#endif  // DEBUG

#define NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR()        \
  NewTypeError(                                     \
      MessageTemplate::kInvalidArgumentForTemporal, \
      isolate->factory()->NewStringFromStaticChars(TEMPORAL_DEBUG_INFO))

#define NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR()        \
  NewRangeError(                                     \
      MessageTemplate::kInvalidTimeValueForTemporal, \
      isolate->factory()->NewStringFromStaticChars(TEMPORAL_DEBUG_INFO))

}  // namespace temporal

namespace {

// #sec-defaulttimezone
// This need to sync with Intl in intl-objects.* js-date-time-format.cc

#ifdef V8_INTL_SUPPORT
MaybeHandle<String> DefaultTimeZone(Isolate* isolate) {
  return Intl::DefaultTimeZone(isolate);
}
#else   //  V8_INTL_SUPPORT
MaybeHandle<String> DefaultTimeZone(Isolate* isolate) {
  // For now, always return "UTC"
  return isolate->factory()->UTC_string();
}
#endif  // V8_INTL_SUPPORT

// #sec-temporal-isodatetimewithinlimits
bool ISODateTimeWithinLimits(Isolate* isolate, int32_t year, int32_t month,
                             int32_t day, int32_t hour, int32_t minute,
                             int32_t second, int32_t millisecond,
                             int32_t microsecond, int32_t nanosecond) {
  /**
   * Note: It is really overkill to decide within the limit by following the
   * specified algorithm literally, which require the conversion to BigInt.
   * Take a short cut and use pre-calculated year/month/day boundary instead.
   *
   * Math:
   * (-8.64 x 10^21- 8.64 x 10^16,  8.64 x 10^21 + 8.64 x 10^16) ns
   * = (-8.64 x 9999 x 10^16,  8.64 x 9999 x 10^16) ns
   * = (-8.64 x 9999 x 10^10,  8.64 x 9999 x 10^10) millisecond
   * = (-8.64 x 9999 x 10^7,  8.64 x 9999 x 10^7) second
   * = (-86400 x 9999 x 10^3,  86400 x 9999 x 10^3) second
   * = (-9999 x 10^3,  9999 x 10^3) days => Because 60*60*24 = 86400
   * 9999000 days is about 27376 years, 4 months and 7 days.
   * Therefore 9999000 days before Jan 1 1970 is around Auguest 23, -25407 and
   * 9999000 days after Jan 1 1970 is around April 9, 29346.
   */
  if (year > -25407 && year < 29346) return true;
  if (year < -25407 || year > 29346) return false;
  if (year == -25407) {
    if (month > 8) return true;
    if (month < 8) return false;
    return (day > 23);
  } else {
    DCHECK_EQ(year, 29346);
    if (month > 4) return false;
    if (month < 4) return true;
    return (day > 23);
  }
  // 1. Assert: year, month, day, hour, minute, second, millisecond,
  // microsecond, and nanosecond are integers.
  // 2. Let ns be ! GetEpochFromISOParts(year, month, day, hour, minute,
  // second, millisecond, microsecond, nanosecond).
  // 3. If ns ≤ -8.64 × 10^21 - 8.64 × 10^16, then
  // 4. If ns ≥ 8.64 × 10^21 + 8.64 × 10^16, then
  // 5. Return true.
}

// #sec-temporal-isoyearmonthwithinlimits
bool ISOYearMonthWithinLimits(int32_t year, int32_t month) {
  // 1. Assert: year and month are integers.
  // 2. If year < −271821 or year > 275760, then
  return !((year < -271821 || year > 275760) ||
           // a. Return false.
           // 3. If year is −271821 and month < 4, then
           (year == -271821 && month < 4) ||
           // a. Return false.
           // 4. If year is 275760 and month > 9, then
           (year == 275760 && month > 9));
  // a. Return false.
  // 5. Return true.
}

}  // namespace

#define ORDINARY_CREATE_FROM_CONSTRUCTOR(obj, target, new_target, T)       \
  Handle<JSReceiver> new_target_receiver =                                 \
      Handle<JSReceiver>::cast(new_target);                                \
  Handle<Map> map;                                                         \
  ASSIGN_RETURN_ON_EXCEPTION(                                              \
      isolate, map,                                                        \
      JSFunction::GetDerivedMap(isolate, target, new_target_receiver), T); \
  Handle<T> object =                                                       \
      Handle<T>::cast(isolate->factory()->NewFastOrSlowJSObjectFromMap(map));

#define THROW_INVALID_RANGE(T) \
  THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), T);

#define CONSTRUCTOR(name)                                                    \
  Handle<JSFunction>(                                                        \
      JSFunction::cast(                                                      \
          isolate->context().native_context().temporal_##name##_function()), \
      isolate)

namespace temporal {

// #sec-temporal-systemutcepochnanoseconds
MaybeHandle<BigInt> SystemUTCEpochNanoseconds(Isolate* isolate) {
  // 1. Let ns be the approximate current UTC date and time, in nanoseconds
  // since the epoch.
  double ns = V8::GetCurrentPlatform()->CurrentClockTimeMillis() * 1000000.0;
  // 2. Set ns to the result of clamping ns between −8.64 × 1021 and 8.64 ×
  // 1021.
  ns = std::floor(std::max(-8.64e21, std::min(ns, 8.64e21)));
  // 3. Return ℤ(ns).
  return BigInt::FromNumber(isolate, isolate->factory()->NewNumber(ns));
}

// #sec-temporal-createtemporalcalendar
MaybeHandle<JSTemporalCalendar> CreateTemporalCalendar(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<String> identifier) {
  // 1. Assert: ! IsBuiltinCalendar(identifier) is true.
  // 2. If newTarget is not provided, set newTarget to %Temporal.Calendar%.
  // 3. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.Calendar.prototype%", « [[InitializedTemporalCalendar]],
  // [[Identifier]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalCalendar)

  DisallowGarbageCollection no_gc;
  object->set_flags(0);
  // 4. Set object.[[Identifier]] to identifier.
  // 5. Return object.
  return object;
}

MaybeHandle<JSTemporalCalendar> CreateTemporalCalendar(
    Isolate* isolate, Handle<String> identifier) {
  return CreateTemporalCalendar(isolate, CONSTRUCTOR(calendar),
                                CONSTRUCTOR(calendar), identifier);
}

// #sec-temporal-createtemporaldate
MaybeHandle<JSTemporalPlainDate> CreateTemporalDate(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    int32_t iso_year, int32_t iso_month, int32_t iso_day,
    Handle<JSReceiver> calendar) {
  // 1. Assert: isoYear is an integer.
  // 2. Assert: isoMonth is an integer.
  // 3. Assert: isoDay is an integer.
  // 4. Assert: Type(calendar) is Object.
  // 5. If ! IsValidISODate(isoYear, isoMonth, isoDay) is false, throw a
  // RangeError exception.
  if (!IsValidISODate(isolate, iso_year, iso_month, iso_day)) {
    THROW_INVALID_RANGE(JSTemporalPlainDate);
  }
  // 6. If ! ISODateTimeWithinLimits(isoYear, isoMonth, isoDay, 12, 0, 0, 0, 0,
  // 0) is false, throw a RangeError exception.
  if (!ISODateTimeWithinLimits(isolate, iso_year, iso_month, iso_day, 12, 0, 0,
                               0, 0, 0)) {
    THROW_INVALID_RANGE(JSTemporalPlainDate);
  }
  // 7. If newTarget is not present, set it to %Temporal.PlainDate%.

  // 8. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.PlainDate.prototype%", « [[InitializedTemporalDate]],
  // [[ISOYear]], [[ISOMonth]], [[ISODay]], [[Calendar]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalPlainDate)
  DisallowGarbageCollection no_gc;
  object->set_year_month_day(0);
  // 9. Set object.[[ISOYear]] to isoYear.
  object->set_iso_year(iso_year);
  // 10. Set object.[[ISOMonth]] to isoMonth.
  object->set_iso_month(iso_month);
  // 11. Set object.[[ISODay]] to isoDay.
  object->set_iso_day(iso_day);
  // 12. Set object.[[Calendar]] to calendar.
  object->set_calendar(*calendar);
  // 13. Return object.
  return object;
}

MaybeHandle<JSTemporalPlainDate> CreateTemporalDate(
    Isolate* isolate, int32_t iso_year, int32_t iso_month, int32_t iso_day,
    Handle<JSReceiver> calendar) {
  return CreateTemporalDate(isolate, CONSTRUCTOR(plain_date),
                            CONSTRUCTOR(plain_date), iso_year, iso_month,
                            iso_day, calendar);
}

// #sec-temporal-createtemporaldatetime
MaybeHandle<JSTemporalPlainDateTime> CreateTemporalDateTime(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    int32_t iso_year, int32_t iso_month, int32_t iso_day, int32_t hour,
    int32_t minute, int32_t second, int32_t millisecond, int32_t microsecond,
    int32_t nanosecond, Handle<JSReceiver> calendar) {
  // 1. Assert: isoYear, isoMonth, isoDay, hour, minute, second, millisecond,
  // microsecond, and nanosecond are integers.
  // 2. Assert: Type(calendar) is Object.
  // 3. If ! IsValidISODate(isoYear, isoMonth, isoDay) is false, throw a
  // RangeError exception.
  if (!IsValidISODate(isolate, iso_year, iso_month, iso_day)) {
    THROW_INVALID_RANGE(JSTemporalPlainDateTime);
  }
  // 4. If ! IsValidTime(hour, minute, second, millisecond, microsecond,
  // nanosecond) is false, throw a RangeError exception.
  if (!IsValidTime(isolate, hour, minute, second, millisecond, microsecond,
                   nanosecond)) {
    THROW_INVALID_RANGE(JSTemporalPlainDateTime);
  }
  // 5. If ! ISODateTimeWithinLimits(isoYear, isoMonth, isoDay, hour, minute,
  // second, millisecond, microsecond, nanosecond) is false, then
  if (!ISODateTimeWithinLimits(isolate, iso_year, iso_month, iso_day, hour,
                               minute, second, millisecond, microsecond,
                               nanosecond)) {
    // a. Throw a RangeError exception.
    THROW_INVALID_RANGE(JSTemporalPlainDateTime);
  }
  // 6. If newTarget is not present, set it to %Temporal.PlainDateTime%.
  // 7. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.PlainDateTime.prototype%", « [[InitializedTemporalDateTime]],
  // [[ISOYear]], [[ISOMonth]], [[ISODay]], [[ISOHour]], [[ISOMinute]],
  // [[ISOSecond]], [[ISOMillisecond]], [[ISOMicrosecond]], [[ISONanosecond]],
  // [[Calendar]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalPlainDateTime)

  DisallowGarbageCollection no_gc;
  object->set_year_month_day(0);
  object->set_hour_minute_second(0);
  object->set_second_parts(0);
  // 8. Set object.[[ISOYear]] to isoYear.
  object->set_iso_year(iso_year);
  // 9. Set object.[[ISOMonth]] to isoMonth.
  object->set_iso_month(iso_month);
  // 10. Set object.[[ISODay]] to isoDay.
  object->set_iso_day(iso_day);
  // 11. Set object.[[ISOHour]] to hour.
  object->set_iso_hour(hour);
  // 12. Set object.[[ISOMinute]] to minute.
  object->set_iso_minute(minute);
  // 13. Set object.[[ISOSecond]] to second.
  object->set_iso_second(second);
  // 14. Set object.[[ISOMillisecond]] to millisecond.
  object->set_iso_millisecond(millisecond);
  // 15. Set object.[[ISOMicrosecond]] to microsecond.
  object->set_iso_microsecond(microsecond);
  // 16. Set object.[[ISONanosecond]] to nanosecond.
  object->set_iso_nanosecond(nanosecond);
  // 17. Set object.[[Calendar]] to calendar.
  object->set_calendar(*calendar);
  // 18. Return object.
  return object;
}

MaybeHandle<JSTemporalPlainDateTime> CreateTemporalDateTime(
    Isolate* isolate, int32_t iso_year, int32_t iso_month, int32_t iso_day,
    int32_t hour, int32_t minute, int32_t second, int32_t millisecond,
    int32_t microsecond, int32_t nanosecond, Handle<JSReceiver> calendar) {
  return CreateTemporalDateTime(isolate, CONSTRUCTOR(plain_date_time),
                                CONSTRUCTOR(plain_date_time), iso_year,
                                iso_month, iso_day, hour, minute, second,
                                millisecond, microsecond, nanosecond, calendar);
}

// #sec-temporal-createtemporaltime
MaybeHandle<JSTemporalPlainTime> CreateTemporalTime(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    int32_t hour, int32_t minute, int32_t second, int32_t millisecond,
    int32_t microsecond, int32_t nanosecond) {
  // 2. If ! IsValidTime(hour, minute, second, millisecond, microsecond,
  // nanosecond) is false, throw a RangeError exception.
  if (!IsValidTime(isolate, hour, minute, second, millisecond, microsecond,
                   nanosecond)) {
    THROW_INVALID_RANGE(JSTemporalPlainTime);
  }

  // 4. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.PlainTime.prototype%", « [[InitializedTemporalTime]],
  // [[ISOHour]], [[ISOMinute]], [[ISOSecond]], [[ISOMillisecond]],
  // [[ISOMicrosecond]], [[ISONanosecond]], [[Calendar]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalPlainTime)
  Handle<JSTemporalCalendar> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar, GetISO8601Calendar(isolate),
                             JSTemporalPlainTime);
  DisallowGarbageCollection no_gc;
  object->set_hour_minute_second(0);
  object->set_second_parts(0);
  // 5. Set object.[[ISOHour]] to hour.
  object->set_iso_hour(hour);
  // 6. Set object.[[ISOMinute]] to minute.
  object->set_iso_minute(minute);
  // 7. Set object.[[ISOSecond]] to second.
  object->set_iso_second(second);
  // 8. Set object.[[ISOMillisecond]] to millisecond.
  object->set_iso_millisecond(millisecond);
  // 9. Set object.[[ISOMicrosecond]] to microsecond.
  object->set_iso_microsecond(microsecond);
  // 10. Set object.[[ISONanosecond]] to nanosecond.
  object->set_iso_nanosecond(nanosecond);
  // 11. Set object.[[Calendar]] to ? GetISO8601Calendar().
  object->set_calendar(*calendar);

  // 12. Return object.
  return object;
}

MaybeHandle<JSTemporalPlainTime> CreateTemporalTime(
    Isolate* isolate, int32_t hour, int32_t minute, int32_t second,
    int32_t millisecond, int32_t microsecond, int32_t nanosecond) {
  return CreateTemporalTime(isolate, CONSTRUCTOR(plain_time),
                            CONSTRUCTOR(plain_time), hour, minute, second,
                            millisecond, microsecond, nanosecond);
}

MaybeHandle<JSTemporalPlainMonthDay> CreateTemporalMonthDay(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    int32_t iso_month, int32_t iso_day, Handle<JSReceiver> calendar,
    int32_t reference_iso_year) {
  // 1. Assert: isoMonth, isoDay, and referenceISOYear are integers.
  // 2. Assert: Type(calendar) is Object.
  // 3. If ! IsValidISODate(referenceISOYear, isoMonth, isoDay) is false, throw
  // a RangeError exception.
  if (!IsValidISODate(isolate, reference_iso_year, iso_month, iso_day)) {
    THROW_INVALID_RANGE(JSTemporalPlainMonthDay);
  }
  // 4. If newTarget is not present, set it to %Temporal.PlainMonthDay%.
  // 5. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.PlainMonthDay.prototype%", « [[InitializedTemporalMonthDay]],
  // [[ISOMonth]], [[ISODay]], [[ISOYear]], [[Calendar]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalPlainMonthDay)
  DisallowGarbageCollection no_gc;
  object->set_year_month_day(0);
  // 6. Set object.[[ISOMonth]] to isoMonth.
  object->set_iso_month(iso_month);
  // 7. Set object.[[ISODay]] to isoDay.
  object->set_iso_day(iso_day);
  // 8. Set object.[[Calendar]] to calendar.
  object->set_calendar(*calendar);
  // 9. Set object.[[ISOYear]] to referenceISOYear.
  object->set_iso_year(reference_iso_year);
  // 10. Return object.
  return object;
}

MaybeHandle<JSTemporalPlainMonthDay> CreateTemporalMonthDay(
    Isolate* isolate, int32_t iso_month, int32_t iso_day,
    Handle<JSReceiver> calendar, int32_t reference_iso_year) {
  return CreateTemporalMonthDay(isolate, CONSTRUCTOR(plain_month_day),
                                CONSTRUCTOR(plain_month_day), iso_month,
                                iso_day, calendar, reference_iso_year);
}

// #sec-temporal-createtemporalyearmonth
MaybeHandle<JSTemporalPlainYearMonth> CreateTemporalYearMonth(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    int32_t iso_year, int32_t iso_month, Handle<JSReceiver> calendar,
    int32_t reference_iso_day) {
  // 1. Assert: isoYear, isoMonth, and referenceISODay are integers.
  // 2. Assert: Type(calendar) is Object.
  // 3. If ! IsValidISODate(isoYear, isoMonth, referenceISODay) is false, throw
  // a RangeError exception.
  if (!IsValidISODate(isolate, iso_year, iso_month, reference_iso_day)) {
    THROW_INVALID_RANGE(JSTemporalPlainYearMonth);
  }
  // 4. If ! ISOYearMonthWithinLimits(isoYear, isoMonth) is false, throw a
  // RangeError exception.
  if (!ISOYearMonthWithinLimits(iso_year, iso_month)) {
    THROW_INVALID_RANGE(JSTemporalPlainYearMonth);
  }
  // 5. If newTarget is not present, set it to %Temporal.PlainYearMonth%.
  // 6. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.PlainYearMonth.prototype%", « [[InitializedTemporalYearMonth]],
  // [[ISOYear]], [[ISOMonth]], [[ISODay]], [[Calendar]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalPlainYearMonth)
  DisallowGarbageCollection no_gc;
  object->set_year_month_day(0);
  // 7. Set object.[[ISOYear]] to isoYear.
  object->set_iso_year(iso_year);
  // 8. Set object.[[ISOMonth]] to isoMonth.
  object->set_iso_month(iso_month);
  // 9. Set object.[[Calendar]] to calendar.
  object->set_calendar(*calendar);
  // 10. Set object.[[ISODay]] to referenceISODay.
  object->set_iso_day(reference_iso_day);
  // 11. Return object.
  return object;
}

MaybeHandle<JSTemporalPlainYearMonth> CreateTemporalYearMonth(
    Isolate* isolate, int32_t iso_year, int32_t iso_month,
    Handle<JSReceiver> calendar, int32_t reference_iso_day) {
  return CreateTemporalYearMonth(isolate, CONSTRUCTOR(plain_year_month),
                                 CONSTRUCTOR(plain_year_month), iso_year,
                                 iso_month, calendar, reference_iso_day);
}

// #sec-temporal-createtemporalzoneddatetime
MaybeHandle<JSTemporalZonedDateTime> CreateTemporalZonedDateTime(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<BigInt> epoch_nanoseconds, Handle<JSReceiver> time_zone,
    Handle<JSReceiver> calendar) {
  // 1. Assert: Type(epochNanoseconds) is BigInt.
  // 2. Assert: ! IsValidEpochNanoseconds(epochNanoseconds) is true.
  CHECK(IsValidEpochNanoseconds(isolate, epoch_nanoseconds));
  // 3. Assert: Type(timeZone) is Object.
  // 4. Assert: Type(calendar) is Object.
  // 5. If newTarget is not present, set it to %Temporal.ZonedDateTime%.
  // 6. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.ZonedDateTime.prototype%", «
  // [[InitializedTemporalZonedDateTime]], [[Nanoseconds]], [[TimeZone]],
  // [[Calendar]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalZonedDateTime)
  DisallowGarbageCollection no_gc;
  // 7. Set object.[[Nanoseconds]] to epochNanoseconds.
  object->set_nanoseconds(*epoch_nanoseconds);
  // 8. Set object.[[TimeZone]] to timeZone.
  object->set_time_zone(*time_zone);
  // 9. Set object.[[Calendar]] to calendar.
  object->set_calendar(*calendar);
  // 10. Return object.
  return object;
}

MaybeHandle<JSTemporalZonedDateTime> CreateTemporalZonedDateTime(
    Isolate* isolate, Handle<BigInt> epoch_nanoseconds,
    Handle<JSReceiver> time_zone, Handle<JSReceiver> calendar) {
  return CreateTemporalZonedDateTime(isolate, CONSTRUCTOR(zoned_date_time),
                                     CONSTRUCTOR(zoned_date_time),
                                     epoch_nanoseconds, time_zone, calendar);
}

// #sec-temporal-createtemporalduration
MaybeHandle<JSTemporalDuration> CreateTemporalDuration(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    double years, double months, double weeks, double days, double hours,
    double minutes, double seconds, double milliseconds, double microseconds,
    double nanoseconds) {
  Factory* factory = isolate->factory();
  // 1. If ! IsValidDuration(years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds) is false, throw a
  // RangeError exception.
  if (!IsValidDuration(isolate,
                       {years, months, weeks, days, hours, minutes, seconds,
                        milliseconds, microseconds, nanoseconds})) {
    THROW_INVALID_RANGE(JSTemporalDuration);
  }

  // 2. If newTarget is not present, set it to %Temporal.Duration%.
  // 3. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.Duration.prototype%", « [[InitializedTemporalDuration]],
  // [[Years]], [[Months]], [[Weeks]], [[Days]], [[Hours]], [[Minutes]],
  // [[Seconds]], [[Milliseconds]], [[Microseconds]], [[Nanoseconds]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalDuration)
  DisallowGarbageCollection no_gc;
  // 4. Set object.[[Years]] to years.
  object->set_years(*(factory->NewNumber(std::floor(years))));
  // 5. Set object.[[Months]] to months.
  object->set_months(*(factory->NewNumber(std::floor(months))));
  // 6. Set object.[[Weeks]] to weeks.
  object->set_weeks(*(factory->NewNumber(std::floor(weeks))));
  // 7. Set object.[[Days]] to days.
  object->set_days(*(factory->NewNumber(std::floor(days))));
  // 8. Set object.[[Hours]] to hours.
  object->set_hours(*(factory->NewNumber(std::floor(hours))));
  // 9. Set object.[[Minutes]] to minutes.
  object->set_minutes(*(factory->NewNumber(std::floor(minutes))));
  // 10. Set object.[[Seconds]] to seconds.
  object->set_seconds(*(factory->NewNumber(std::floor(seconds))));
  // 11. Set object.[[Milliseconds]] to milliseconds.
  object->set_milliseconds(*(factory->NewNumber(std::floor(milliseconds))));
  // 12. Set object.[[Microseconds]] to microseconds.
  object->set_microseconds(*(factory->NewNumber(std::floor(microseconds))));
  // 13. Set object.[[Nanoseconds]] to nanoseconds.
  object->set_nanoseconds(*(factory->NewNumber(std::floor(nanoseconds))));
  // 14. Return object.
  return object;
}

MaybeHandle<JSTemporalDuration> CreateTemporalDuration(
    Isolate* isolate, double years, double months, double weeks, double days,
    double hours, double minutes, double seconds, double milliseconds,
    double microseconds, double nanoseconds) {
  return CreateTemporalDuration(isolate, CONSTRUCTOR(duration),
                                CONSTRUCTOR(duration), years, months, weeks,
                                days, hours, minutes, seconds, milliseconds,
                                microseconds, nanoseconds);
}

// #sec-temporal-createnegatedtemporalduration
MaybeHandle<JSTemporalDuration> CreateNegatedTemporalDuration(
    Isolate* isolate, Handle<JSTemporalDuration> duration) {
  // 1. Assert: Type(duration) is Object.
  // 2. Assert: duration has an [[InitializedTemporalDuration]] internal slot.
  // 3. Return ! CreateTemporalDuration(−duration.[[Years]],
  // −duration.[[Months]], −duration.[[Weeks]], −duration.[[Days]],
  // −duration.[[Hours]], −duration.[[Minutes]], −duration.[[Seconds]],
  // −duration.[[Milliseconds]], −duration.[[Microseconds]],
  // −duration.[[Nanoseconds]]).

  return temporal::CreateTemporalDuration(
      isolate, -duration->years().Number(), -duration->months().Number(),
      -duration->weeks().Number(), -duration->days().Number(),
      -duration->hours().Number(), -duration->minutes().Number(),
      -duration->seconds().Number(), -duration->milliseconds().Number(),
      -duration->microseconds().Number(), -duration->nanoseconds().Number());
}

// #sec-temporal-createtemporalinstant
MaybeHandle<JSTemporalInstant> CreateTemporalInstant(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<BigInt> epoch_nanoseconds) {
  // 1. Assert: Type(epochNanoseconds) is BigInt.
  // 2. Assert: ! IsValidEpochNanoseconds(epochNanoseconds) is true.
  CHECK(IsValidEpochNanoseconds(isolate, epoch_nanoseconds));

  // 4. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.Instant.prototype%", « [[InitializedTemporalInstant]],
  // [[Nanoseconds]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalInstant)
  DisallowGarbageCollection no_gc;
  // 5. Set object.[[Nanoseconds]] to ns.
  object->set_nanoseconds(*epoch_nanoseconds);
  return object;
}

MaybeHandle<JSTemporalInstant> CreateTemporalInstant(
    Isolate* isolate, Handle<BigInt> epoch_nanoseconds) {
  return CreateTemporalInstant(isolate, CONSTRUCTOR(instant),
                               CONSTRUCTOR(instant), epoch_nanoseconds);
}

namespace {

MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZoneFromIndex(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    int32_t index) {
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalTimeZone)
  DisallowGarbageCollection no_gc;
  object->set_flags(0);

  object->set_is_offset(false);
  object->set_offset_milliseconds_or_time_zone_index(index);
  return object;
}

MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZoneUTC(
    Isolate* isolate, Handle<JSFunction> target,
    Handle<HeapObject> new_target) {
  return CreateTemporalTimeZoneFromIndex(isolate, target, new_target, 0);
}

MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    const std::string& identifier) {
  // 1. If newTarget is not present, set it to %Temporal.TimeZone%.
  // 2. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.TimeZone.prototype%", « [[InitializedTemporalTimeZone]],
  // [[Identifier]], [[OffsetNanoseconds]] »).
  // 3. Set object.[[Identifier]] to identifier.
  if (IsUTC(isolate, identifier)) {
    return CreateTemporalTimeZoneUTC(isolate, target, new_target);
  }
#ifdef V8_INTL_SUPPORT
  int32_t time_zone_index;
  Maybe<bool> maybe_time_zone_index =
      Intl::GetTimeZoneIndex(isolate, identifier, &time_zone_index);
  MAYBE_RETURN(maybe_time_zone_index, Handle<JSTemporalTimeZone>());
  if (maybe_time_zone_index.FromJust()) {
    return CreateTemporalTimeZoneFromIndex(isolate, target, new_target,
                                           time_zone_index);
  }
#endif  // V8_INTL_SUPPORT

  // 4. If identifier satisfies the syntax of a TimeZoneNumericUTCOffset
  // (see 13.33), then a. Set object.[[OffsetNanoseconds]] to !
  // ParseTimeZoneOffsetString(identifier).
  // 5. Else,
  // a. Assert: ! CanonicalizeTimeZoneName(identifier) is identifier.
  // b. Set object.[[OffsetNanoseconds]] to undefined.
  // 6. Return object.
  Handle<String> identifier_str =
      isolate->factory()->NewStringFromAsciiChecked(identifier.c_str());
  Maybe<int64_t> maybe_offset_nanoseconds =
      ParseTimeZoneOffsetString(isolate, identifier_str, false);
  MAYBE_RETURN(maybe_offset_nanoseconds, Handle<JSTemporalTimeZone>());
  int64_t offset_nanoseconds = maybe_offset_nanoseconds.FromJust();
  int32_t offset_milliseconds =
      static_cast<int32_t>(offset_nanoseconds / 1000000);

  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalTimeZone)
  DisallowGarbageCollection no_gc;
  object->set_flags(0);

  object->set_is_offset(true);
  object->set_offset_milliseconds_or_time_zone_index(offset_milliseconds);
  return object;
}
}  // namespace

// #sec-temporal-createtemporaltimezone
MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<String> identifier) {
  return CreateTemporalTimeZone(isolate, target, new_target,
                                identifier->ToCString().get());
}

MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, const std::string& identifier) {
  return CreateTemporalTimeZone(isolate, CONSTRUCTOR(time_zone),
                                CONSTRUCTOR(time_zone), identifier);
}

MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, Handle<String> identifier) {
  return CreateTemporalTimeZone(isolate, CONSTRUCTOR(time_zone),
                                CONSTRUCTOR(time_zone), identifier);
}

MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZoneUTC(Isolate* isolate) {
  return CreateTemporalTimeZoneUTC(isolate, CONSTRUCTOR(time_zone),
                                   CONSTRUCTOR(time_zone));
}

// #sec-temporal-systeminstant
MaybeHandle<JSTemporalInstant> SystemInstant(Isolate* isolate) {
  // 1. Let ns be ! SystemUTCEpochNanoseconds().
  Handle<BigInt> ns;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, ns, SystemUTCEpochNanoseconds(isolate),
                             JSTemporalInstant);
  // 2. Return ? CreateTemporalInstant(ns).
  return CreateTemporalInstant(isolate, ns);
}

// #sec-temporal-systemtimezone
MaybeHandle<JSTemporalTimeZone> SystemTimeZone(Isolate* isolate) {
  Handle<String> default_time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, default_time_zone,
                             DefaultTimeZone(isolate), JSTemporalTimeZone);
  return CreateTemporalTimeZone(isolate, default_time_zone);
}

namespace {

Maybe<DateTimeRecordCommon> GetISOPartsFromEpoch(
    Isolate* isolate, Handle<BigInt> epoch_nanoseconds) {
  DateTimeRecordCommon result;
  // 1. Let remainderNs be epochNanoseconds modulo 106.
  Handle<BigInt> million = BigInt::FromInt64(isolate, 1000000);
  Handle<BigInt> remainder_ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, remainder_ns,
      BigInt::Remainder(isolate, epoch_nanoseconds, million),
      Nothing<DateTimeRecordCommon>());
  // Need to do some remainder magic to negative remainder.
  if (remainder_ns->IsNegative()) {
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, remainder_ns, BigInt::Add(isolate, remainder_ns, million),
        Nothing<DateTimeRecordCommon>());
  }

  // 2. Let epochMilliseconds be (epochNanoseconds − remainderNs) / 106.
  Handle<BigInt> bigint;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, bigint,
      BigInt::Subtract(isolate, epoch_nanoseconds, remainder_ns),
      Nothing<DateTimeRecordCommon>());
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, bigint,
                                   BigInt::Divide(isolate, bigint, million),
                                   Nothing<DateTimeRecordCommon>());
  int64_t epoch_milliseconds = bigint->AsInt64();
  int year = 0;
  int month = 0;
  int day = 0;
  int wday = 0;
  int hour = 0;
  int min = 0;
  int sec = 0;
  int ms = 0;
  isolate->date_cache()->BreakDownTime(epoch_milliseconds, &year, &month, &day,
                                       &wday, &hour, &min, &sec, &ms);

  // 3. Let year be ! YearFromTime(epochMilliseconds).
  result.year = year;
  // 4. Let month be ! MonthFromTime(epochMilliseconds) + 1.
  result.month = month + 1;
  CHECK_GE(result.month, 1);
  CHECK_LE(result.month, 12);
  // 5. Let day be ! DateFromTime(epochMilliseconds).
  result.day = day;
  CHECK_GE(result.day, 1);
  CHECK_LE(result.day, 31);
  // 6. Let hour be ! HourFromTime(epochMilliseconds).
  result.hour = hour;
  CHECK_GE(result.hour, 0);
  CHECK_LE(result.hour, 23);
  // 7. Let minute be ! MinFromTime(epochMilliseconds).
  result.minute = min;
  CHECK_GE(result.minute, 0);
  CHECK_LE(result.minute, 59);
  // 8. Let second be ! SecFromTime(epochMilliseconds).
  result.second = sec;
  CHECK_GE(result.second, 0);
  CHECK_LE(result.second, 59);
  // 9. Let millisecond be ! msFromTime(epochMilliseconds).
  result.millisecond = ms;
  CHECK_GE(result.millisecond, 0);
  CHECK_LE(result.millisecond, 999);
  // 10. Let microsecond be floor(remainderNs / 1000) modulo 1000.
  int64_t remainder = remainder_ns->AsInt64();
  result.microsecond = (remainder / 1000) % 1000;
  CHECK_GE(result.microsecond, 0);
  CHECK_LE(result.microsecond, 999);
  // 11. Let nanosecond be remainderNs modulo 1000.
  result.nanosecond = remainder % 1000;
  CHECK_GE(result.nanosecond, 0);
  CHECK_LE(result.nanosecond, 999);
  return Just(result);
}

}  // namespace

namespace {
// #sec-temporal-balanceisodatetime
DateTimeRecordCommon BalanceISODateTime(Isolate* isolate, int32_t year,
                                        int32_t month, int32_t day,
                                        int32_t hour, int32_t minute,
                                        int32_t second, int32_t millisecond,
                                        int32_t microsecond,
                                        int64_t nanosecond) {
  // 1. Assert: year, month, day, hour, minute, second, millisecond,
  // microsecond, and nanosecond are integers.
  // 2. Let balancedTime be ! BalanceTime(hour, minute, second, millisecond,
  // microsecond, nanosecond).
  DateTimeRecordCommon balanced_time = BalanceTime(
      isolate, hour, minute, second, millisecond, microsecond, nanosecond);
  // 3. Let balancedDate be ! BalanceISODate(year, month, day +
  // balancedTime.[[Days]]).
  day += balanced_time.day;
  BalanceISODate(isolate, &year, &month, &day);
  // 4. Return the Record { [[Year]]: balancedDate.[[Year]], [[Month]]:
  // balancedDate.[[Month]], [[Day]]: balancedDate.[[Day]], [[Hour]]:
  // balancedTime.[[Hour]], [[Minute]]: balancedTime.[[Minute]], [[Second]]:
  // balancedTime.[[Second]], [[Millisecond]]: balancedTime.[[Millisecond]],
  // [[Microsecond]]: balancedTime.[[Microsecond]], [[Nanosecond]]:
  // balancedTime.[[Nanosecond]] }.
  return {year,
          month,
          day,
          balanced_time.hour,
          balanced_time.minute,
          balanced_time.second,
          balanced_time.millisecond,
          balanced_time.microsecond,
          balanced_time.nanosecond};
}

}  // namespace

MaybeHandle<JSTemporalPlainDateTime> BuiltinTimeZoneGetPlainDateTimeFor(
    Isolate* isolate, Handle<JSReceiver> time_zone,
    Handle<JSTemporalInstant> instant, Handle<JSReceiver> calendar) {
  // 1. Let offsetNanoseconds be ? GetOffsetNanosecondsFor(timeZone, instant).
  Maybe<int64_t> maybe_offset_nanoseconds =
      GetOffsetNanosecondsFor(isolate, time_zone, instant);
  MAYBE_RETURN(maybe_offset_nanoseconds, Handle<JSTemporalPlainDateTime>());
  // 2. Let result be ! GetISOPartsFromEpoch(instant.[[Nanoseconds]]).
  Maybe<DateTimeRecordCommon> maybe_result = GetISOPartsFromEpoch(
      isolate, Handle<BigInt>(instant->nanoseconds(), isolate));
  MAYBE_RETURN(maybe_result, Handle<JSTemporalPlainDateTime>());
  int64_t offset_nanoseconds = maybe_offset_nanoseconds.FromJust();

  // 3. Set result to ! BalanceISODateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]] +
  // offsetNanoseconds).
  DateTimeRecordCommon result = maybe_result.FromJust();
  result = BalanceISODateTime(isolate, result.year, result.month, result.day,
                              result.hour, result.minute, result.second,
                              result.millisecond, result.microsecond,
                              offset_nanoseconds + result.nanosecond);
  // 4. Return ? CreateTemporalDateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // calendar).
  return CreateTemporalDateTime(isolate, result.year, result.month, result.day,
                                result.hour, result.minute, result.second,
                                result.millisecond, result.microsecond,
                                result.nanosecond, calendar);
}
// #sec-temporal-totemporalcalendar
MaybeHandle<JSReceiver> ToTemporalCalendar(
    Isolate* isolate, Handle<Object> temporal_calendar_like,
    const char* method) {
  Factory* factory = isolate->factory();
  // 1.If Type(temporalCalendarLike) is Object, then
  if (temporal_calendar_like->IsJSReceiver()) {
    // a. If temporalCalendarLike has an [[InitializedTemporalDate]],
    // [[InitializedTemporalDateTime]], [[InitializedTemporalMonthDay]],
    // [[InitializedTemporalTime]], [[InitializedTemporalYearMonth]], or
    // [[InitializedTemporalZonedDateTime]] internal slot, then i. Return
    // temporalCalendarLike.[[Calendar]].

#define EXTRACT_CALENDAR(T, obj)                                            \
  if (obj->IsJSTemporal##T()) {                                             \
    return Handle<JSReceiver>(Handle<JSTemporal##T>::cast(obj)->calendar(), \
                              isolate);                                     \
  }

    EXTRACT_CALENDAR(PlainDate, temporal_calendar_like)
    EXTRACT_CALENDAR(PlainDateTime, temporal_calendar_like)
    EXTRACT_CALENDAR(PlainMonthDay, temporal_calendar_like)
    EXTRACT_CALENDAR(PlainTime, temporal_calendar_like)
    EXTRACT_CALENDAR(PlainYearMonth, temporal_calendar_like)
    EXTRACT_CALENDAR(ZonedDateTime, temporal_calendar_like)

#undef EXTRACT_CALENDAR
    Handle<JSReceiver> obj = Handle<JSReceiver>::cast(temporal_calendar_like);

    // b. If ? HasProperty(temporalCalendarLike, "calendar") is false, return
    // temporalCalendarLike.
    Maybe<bool> maybe_has =
        JSReceiver::HasProperty(obj, factory->calendar_string());

    MAYBE_RETURN(maybe_has, Handle<JSReceiver>());
    if (!maybe_has.FromJust()) {
      return obj;
    }
    // c.  Set temporalCalendarLike to ? Get(temporalCalendarLike, "calendar").
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_calendar_like,
        JSReceiver::GetProperty(isolate, obj, factory->calendar_string()),
        JSReceiver);
    // d. If Type(temporalCalendarLike) is Object
    if (temporal_calendar_like->IsJSReceiver()) {
      obj = Handle<JSReceiver>::cast(temporal_calendar_like);
      // and ? HasProperty(temporalCalendarLike, "calendar") is false,
      maybe_has = JSReceiver::HasProperty(obj, factory->calendar_string());
      MAYBE_RETURN(maybe_has, Handle<JSReceiver>());
      if (!maybe_has.FromJust()) {
        // return temporalCalendarLike.
        return obj;
      }
    }
  }

  // 2. Let identifier be ? ToString(temporalCalendarLike).
  Handle<String> identifier;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, identifier,
                             Object::ToString(isolate, temporal_calendar_like),
                             JSReceiver);
  // 3. If ! IsBuiltinCalendar(identifier) is false, then
  if (!IsBuiltinCalendar(isolate, identifier)) {
    // a. Let identifier be ? ParseTemporalCalendarString(identifier).
    Maybe<std::string> maybe_identifier =
        ParseTemporalCalendarString(isolate, identifier);
    MAYBE_RETURN(maybe_identifier, Handle<JSTemporalCalendar>());
    identifier =
        factory->NewStringFromAsciiChecked(maybe_identifier.FromJust().c_str());
  }
  // 4. Return ? CreateTemporalCalendar(identifier).
  return CreateTemporalCalendar(isolate, identifier);
}

// #sec-temporal-totemporalcalendarwithisodefault
MaybeHandle<JSReceiver> ToTemporalCalendarWithISODefault(
    Isolate* isolate, Handle<Object> temporal_calendar_like,
    const char* method) {
  TEMPORAL_ENTER_FUNC

  // 1. If temporalCalendarLike is undefined, then
  if (temporal_calendar_like->IsUndefined()) {
    // a. Return ? GetISO8601Calendar().
    return GetISO8601Calendar(isolate);
  }
  // 2. Return ? ToTemporalCalendar(temporalCalendarLike).
  return ToTemporalCalendar(isolate, temporal_calendar_like, method);
}
// #sec-temporal-totemporaltimezone
MaybeHandle<JSReceiver> ToTemporalTimeZone(
    Isolate* isolate, Handle<Object> temporal_time_zone_like,
    const char* method) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  // 1. If Type(temporalTimeZoneLike) is Object, then
  if (temporal_time_zone_like->IsJSReceiver()) {
    // a. If temporalTimeZoneLike has an [[InitializedTemporalZonedDateTime]]
    // internal slot, then
    if (temporal_time_zone_like->IsJSTemporalZonedDateTime()) {
      // i. Return temporalTimeZoneLike.[[TimeZone]].
      Handle<JSTemporalZonedDateTime> zoned_date_time =
          Handle<JSTemporalZonedDateTime>::cast(temporal_time_zone_like);
      return Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
    }
    Handle<JSReceiver> obj = Handle<JSReceiver>::cast(temporal_time_zone_like);
    // b. If ? HasProperty(temporalTimeZoneLike, "timeZone") is false,
    Maybe<bool> maybe_has =
        JSReceiver::HasProperty(obj, factory->timeZone_string());
    MAYBE_RETURN(maybe_has, Handle<JSReceiver>());
    if (!maybe_has.FromJust()) {
      // return temporalTimeZoneLike.
      return obj;
    }
    // c. Set temporalTimeZoneLike to ?
    // Get(temporalTimeZoneLike, "timeZone").
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_time_zone_like,
        JSReceiver::GetProperty(isolate, obj, factory->timeZone_string()),
        JSReceiver);
    // d. If Type(temporalTimeZoneLike)
    if (temporal_time_zone_like->IsJSReceiver()) {
      // is Object and ? HasProperty(temporalTimeZoneLike, "timeZone") is false,
      obj = Handle<JSReceiver>::cast(temporal_time_zone_like);
      maybe_has = JSReceiver::HasProperty(obj, factory->timeZone_string());
      MAYBE_RETURN(maybe_has, Handle<JSReceiver>());
      if (!maybe_has.FromJust()) {
        // return temporalTimeZoneLike.
        return obj;
      }
    }
  }
  Handle<String> identifier;
  // 2. Let identifier be ? ToString(temporalTimeZoneLike).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, identifier,
                             Object::ToString(isolate, temporal_time_zone_like),
                             JSReceiver);
  // 3. Let result be ? ParseTemporalTimeZone(identifier).
  Maybe<std::string> maybe_result = ParseTemporalTimeZone(isolate, identifier);
  MAYBE_RETURN(maybe_result, Handle<JSReceiver>());
  std::string result = maybe_result.FromJust();

  // 4. Return ? CreateTemporalTimeZone(result).
  return CreateTemporalTimeZone(isolate, result);
}

// #sec-temporal-formattimezoneoffsetstring
std::string FormatTimeZoneOffsetString(int64_t offset_nanoseconds) {
  // 1. Assert: offsetNanoseconds is an integer.
  // 2. If offsetNanoseconds ≥ 0, let sign be "+"; otherwise, let sign be "-".
  char sign = (offset_nanoseconds >= 0) ? '+' : '-';
  // 3. Let offsetNanoseconds be abs(offsetNanoseconds).
  offset_nanoseconds = abs(offset_nanoseconds);
  // 3. Let nanoseconds be offsetNanoseconds modulo 109.
  int64_t nanoseconds = offset_nanoseconds % 1000000000;
  // 4. Let seconds be floor(offsetNanoseconds / 109) modulo 60.
  int64_t seconds = (offset_nanoseconds / 1000000000) % 60;
  // 5. Let minutes be floor(offsetNanoseconds / (6 × 1010)) modulo 60.
  int64_t minutes = (offset_nanoseconds / 60000000000) % 60;
  // 6. Let hours be floor(offsetNanoseconds / (3.6 × 1012)).
  int64_t hours = offset_nanoseconds / 3600000000000;
  // 7. Let h be hours, formatted as a two-digit decimal number, padded to the
  // left with a zero if necessary.
  // 8. Let m be minutes, formatted as a two-digit decimal number, padded to the
  // left with a zero if necessary.
  // 9. Let s be seconds, formatted as a two-digit decimal number, padded to the
  // left with a zero if necessary.
  // 10. If nanoseconds ≠ 0, then
  std::ostringstream post;
  if (nanoseconds != 0) {
    // a. Let fraction be nanoseconds, formatted as a nine-digit decimal number,
    // padded to the left with zeroes if necessary.
    std::ostringstream fraction_oss;
    int32_t precision_len = 9;
    fraction_oss << std::setfill('0') << std::setw(precision_len)
                 << std::to_string(nanoseconds);
    std::string fraction_str = fraction_oss.str();
    // b. Set fraction to the longest possible substring of fraction starting at
    // position 0 and not ending with the code unit 0x0030 (DIGIT ZERO).
    while (precision_len > 0 && fraction_str[precision_len - 1] == '0') {
      precision_len--;
    }
    // c. Let post be the string-concatenation of the code unit 0x003A (COLON),
    // s, the code unit 0x002E (FULL STOP), and fraction.
    post << ':' << std::setfill('0') << std::setw(2) << seconds << '.'
         << fraction_str.substr(0, precision_len);
    // 11. Else if seconds ≠ 0, then
  } else if (seconds != 0) {
    // a. Let post be the string-concatenation of the code unit 0x003A (COLON)
    // and s.
    post << ':' << std::setfill('0') << std::setw(2) << seconds;
  }
  // 12. Return the string-concatenation of sign, h, the code unit 0x003A
  // (COLON), m, and post.
  std::ostringstream result;
  result << sign << std::setfill('0') << std::setw(2) << hours << ':'
         << std::setfill('0') << std::setw(2) << minutes << post.str();
  return result.str();
}

// #sec-temporal-builtintimezonegetoffsetstringfor
MaybeHandle<String> BuiltinTimeZoneGetOffsetStringFor(
    Isolate* isolate, Handle<JSReceiver> time_zone,
    Handle<JSTemporalInstant> instant) {
  Maybe<std::string> maybe_result =
      BuiltinTimeZoneGetOffsetStdStringFor(isolate, time_zone, instant);
  MAYBE_RETURN(maybe_result, Handle<String>());
  return isolate->factory()->NewStringFromAsciiChecked(
      maybe_result.FromJust().c_str());
}

Maybe<std::string> BuiltinTimeZoneGetOffsetStdStringFor(
    Isolate* isolate, Handle<JSReceiver> time_zone,
    Handle<JSTemporalInstant> instant) {
  // 1. Let offsetNanoseconds be ? GetOffsetNanosecondsFor(timeZone, instant).
  Maybe<int64_t> maybe_offset_nanoseconds =
      GetOffsetNanosecondsFor(isolate, time_zone, instant);
  MAYBE_RETURN(maybe_offset_nanoseconds, Nothing<std::string>());
  int64_t offset_nanoseconds = maybe_offset_nanoseconds.FromJust();

  // 2. Return ! FormatTimeZoneOffsetString(offsetNanoseconds).
  return Just(FormatTimeZoneOffsetString(offset_nanoseconds));
}

namespace {

int32_t StringToInt(const std::string& str) {
  TEMPORAL_ENTER_FUNC

  int32_t digits = 0;
  int32_t sign = 1;
  for (size_t i = 0; i < str.length(); i++) {
    char c = str[i];
    if (c == '-') {
      sign = -1;
    }
    if (c == '+') {
      // do nothing
    } else if ('0' <= c && c <= '9') {
      digits = digits * 10 + (c - '0');
    } else {
      UNREACHABLE();
    }
  }
  return digits * sign;
}

struct ParsedResult {
  std::string date_year;
  std::string date_month;
  std::string date_day;
  std::string time_hour;
  std::string time_minute;
  std::string time_second;
  std::string time_fractional_part;
  std::string calendar_name;
  std::string utc_designator;
  std::string tzuo_sign;             // TimeZoneUTCOffsetSign
  std::string tzuo_hour;             // TimeZoneUTCOffsetHour
  std::string tzuo_minute;           // TimeZoneUTCOffsetMinute
  std::string tzuo_second;           // TimeZoneUTCOffsetSecond
  std::string tzuo_fractional_part;  // TimeZoneUTCOffsetFractionalPart
  std::string tzi_name;              // TimeZoneIANAName

  void clear() {
    date_year.clear();
    date_month.clear();
    date_day.clear();
    time_hour.clear();
    time_minute.clear();
    time_second.clear();
    time_fractional_part.clear();
    calendar_name.clear();
    utc_designator.clear();
    tzuo_sign.clear();
    tzuo_hour.clear();
    tzuo_minute.clear();
    tzuo_second.clear();
    tzuo_fractional_part.clear();
  }
};

struct ParsedDuration {
  std::string sign;
  std::string years;
  std::string months;
  std::string weeks;
  std::string days;
  std::string whole_hours;
  std::string hours_fraction;
  std::string whole_minutes;
  std::string minutes_fraction;
  std::string whole_seconds;
  std::string seconds_fraction;

  void clear() {
    years.clear();
    months.clear();
    weeks.clear();
    days.clear();
    whole_hours.clear();
    hours_fraction.clear();
    whole_minutes.clear();
    minutes_fraction.clear();
    whole_seconds.clear();
    seconds_fraction.clear();
  }
};

#define MINUS_SIGN (0x2212)
#define IN_RANGE(a, b, c) (((a) <= (b)) && ((b) <= (c)))
#define IS_MINUS_SIGN(ch) ((ch) == MINUS_SIGN)
#define IS_ALPHA(ch) (IN_RANGE('a', ch, 'z') || IN_RANGE('A', ch, 'Z'))
#define IS_DIGIT(ch) IN_RANGE('0', ch, '9')
#define IS_NON_ZERO_DIGIT(ch) IN_RANGE('1', ch, '9')
#define IS_CAL_CHAR(ch) (IS_ALPHA(ch) || IS_DIGIT(ch))

#define IS_OR(a, b, c) (((a) == (b)) || ((a) == (c)))
#define IS_TIME_ZONE_UTC_OFFSET_SIGN(ch) IS_SIGN(ch)
#define IS_TZ_LEADING_CHAR(ch) (IS_ALPHA(ch) || IS_OR(ch, '.', '_'))
#define IS_TZ_CHAR(ch) (IS_TZ_LEADING_CHAR(ch) || ((ch) == '-'))
#define CANONICAL_SIGN(ch) (static_cast<char>(IS_MINUS_SIGN(ch) ? '-' : (ch)))

#define IS_DECIMAL_SEPARATOR(ch) IS_OR(ch, '.', ',')
#define IS_ASCII_SIGN(ch) IS_OR(ch, '-', '+')
#define IS_SIGN(ch) (IS_ASCII_SIGN(ch) || IS_MINUS_SIGN(ch))
#define IS_TIME_DESIGNATOR(ch) IS_OR(ch, 'T', 't')
#define IS_DAYS_DESIGNATOR(ch) IS_OR(ch, 'D', 'd')
#define IS_HOURS_DESIGNATOR(ch) IS_OR(ch, 'H', 'h')
#define IS_MINUTES_DESIGNATOR(ch) IS_OR(ch, 'M', 'm')
#define IS_MONTHS_DESIGNATOR(ch) IS_OR(ch, 'M', 'm')
#define IS_DURATION_DESIGNATOR(ch) IS_OR(ch, 'P', 'p')
#define IS_SECONDS_DESIGNATOR(ch) IS_OR(ch, 'S', 's')
#define IS_WEEKS_DESIGNATOR(ch) IS_OR(ch, 'W', 'w')
#define IS_YEARS_DESIGNATOR(ch) IS_OR(ch, 'Y', 'y')
#define IS_UTC_DESIGNATOR(ch) IS_OR(ch, 'Z', 'z')
#define IS_DATE_TIME_SEPARATOR(ch) (((ch) == ' ') || IS_TIME_DESIGNATOR(ch))

// Hour:
//   [0 1] Digit
//   2 [0 1 2 3]
template <typename Char>
bool ScanHour(base::Vector<Char> str, int32_t s, std::string* out,
              int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 2)) return false;
  if (!((IN_RANGE('0', str[s], '1') && IS_DIGIT(str[s + 1])) ||
        ((str[s] == '2') && IN_RANGE('0', str[s + 1], '3'))))
    return false;
  (*out) += str[s];
  (*out) += str[s + 1];
  *out_length = 2;
  return true;
}
// MinuteSecond:
//   [0 1 2 3 4 5] Digit
template <typename Char>
bool ScanMinuteSecond(base::Vector<Char> str, int32_t s, std::string* out,
                      int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 2)) return false;
  if (!(IN_RANGE('0', str[s], '5') && IS_DIGIT(str[s + 1]))) return false;
  (*out) += str[s];
  (*out) += str[s + 1];
  *out_length = 2;
  return true;
}

#define SCAN_FORWARD(T1, T2, R)                          \
  template <typename Char>                               \
  bool Scan##T1(base::Vector<Char> str, int32_t s, R* r, \
                int32_t* out_length) {                   \
    TEMPORAL_ENTER_FUNC                                  \
    bool ret = Scan##T2(str, s, r, out_length);          \
    return ret;                                          \
  }

#define SCAN_EITHER_FORWARD(T1, T2, T3, R)                             \
  template <typename Char>                                             \
  bool Scan##T1(base::Vector<Char> str, int32_t s, R* r, int32_t* l) { \
    TEMPORAL_ENTER_FUNC                                                \
    if (Scan##T2(str, s, r, l)) return true;                           \
    return Scan##T3(str, s, r, l);                                     \
  }

#define SCAN_FORWARD_TO_FIELD(T1, T2, field, R)          \
  template <typename Char>                               \
  bool Scan##T1(base::Vector<Char> str, int32_t s, R* r, \
                int32_t* out_length) {                   \
    TEMPORAL_ENTER_FUNC                                  \
    return Scan##T2(str, s, &(r->field), out_length);    \
  }

#define SCAN_EITHER_FORWARD_TO_FIELD(T1, T2, T3, field, R) \
  template <typename Char>                                 \
  bool Scan##T1(base::Vector<Char> str, int32_t s, R* r,   \
                int32_t* out_length) {                     \
    TEMPORAL_ENTER_FUNC                                    \
    return Scan##T2(str, s, &(r->field), out_length) ||    \
           Scan##T3(str, s, &(r->field), out_length);      \
  }

// TimeHour: Hour
SCAN_FORWARD_TO_FIELD(TimeHour, Hour, time_hour, ParsedResult)

// TimeMinute: MinuteSecond
SCAN_FORWARD_TO_FIELD(TimeMinute, MinuteSecond, time_minute, ParsedResult)

// TimeSecond:
//   MinuteSecond
//   60
template <typename Char>
bool ScanTimeSecond(base::Vector<Char> str, int32_t s, ParsedResult* r,
                    int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (ScanMinuteSecond(str, s, &(r->time_second), out_length)) {
    // MinuteSecond
    return true;
  }
  if (str.length() < (s + 2)) return false;
  if (('6' != str[s] || str[s + 1] != '0')) return false;
  // 60
  r->time_second = str[s];
  r->time_second += str[s + 1];
  *out_length = 2;
  return true;
}

// See PR1796
// FractionalPart : Digit{1,9}
template <typename Char>
bool ScanFractionalPart(base::Vector<Char> str, int32_t s, std::string* out,
                        int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 1)) return false;
  if (!IS_DIGIT(str[s])) return false;
  *out = str[s];
  int32_t len = 1;
  while (((s + len) < str.length()) && (len < 9) && IS_DIGIT(str[s + len])) {
    *out += str[s + len];
    len++;
  }
  *out_length = len;
  return true;
}

// See PR1796
// TimeFraction: FractionalPart
SCAN_FORWARD_TO_FIELD(TimeFractionalPart, FractionalPart, time_fractional_part,
                      ParsedResult)

// Fraction: DecimalSeparator TimeFractionalPart
// DecimalSeparator: one of , .
template <typename Char>
bool ScanFraction(base::Vector<Char> str, int32_t s, ParsedResult* r,
                  int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 2)) return false;
  if (!(IS_DECIMAL_SEPARATOR(str[s]))) return false;
  if (!ScanTimeFractionalPart(str, s + 1, r, out_length)) return false;
  *out_length += 1;
  return true;
}

// TimeFraction: Fraction
SCAN_FORWARD(TimeFraction, Fraction, ParsedResult)

// TimeSpec:
//  TimeHour
//  TimeHour : TimeMinute
//  TimeHour : TimeMinute : TimeSecond [TimeFraction]
//  TimeHour TimeMinute
//  TimeHour TimeMinute TimeSecond [TimeFraction]
template <typename Char>
bool ScanTimeSpec(base::Vector<Char> str, int32_t s, ParsedResult* r,
                  int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  int32_t hour_len = 0;
  if (!ScanTimeHour(str, s, r, &hour_len)) return false;
  if ((s + hour_len) == str.length()) {
    // TimeHour
    *out_length = hour_len;
    return true;
  }
  if (str[s + hour_len] == ':') {
    int32_t minute_len = 0;
    if (!ScanTimeMinute(str, s + hour_len + 1, r, &minute_len)) {
      r->time_hour.clear();
      return false;
    }
    if ((s + hour_len + 1 + minute_len) == str.length() ||
        (str[s + hour_len + 1 + minute_len] != ':')) {
      // TimeHour : TimeMinute
      *out_length = hour_len + 1 + minute_len;
      return true;
    }
    int32_t second_len = 0;
    if (!ScanTimeSecond(str, s + hour_len + 1 + minute_len + 1, r,
                        &second_len)) {
      r->time_hour.clear();
      r->time_minute.clear();
      return false;
    }
    int32_t fraction_len = 0;
    ScanTimeFraction(str, s + hour_len + 1 + minute_len + 1 + second_len, r,
                     &fraction_len);
    // TimeHour : TimeMinute : TimeSecond [TimeFraction]
    *out_length = hour_len + 1 + minute_len + 1 + second_len + fraction_len;
    return true;
  } else {
    int32_t minute_len;
    if (!ScanTimeMinute(str, s + hour_len, r, &minute_len)) {
      // TimeHour
      *out_length = hour_len;
      return true;
    }
    int32_t second_len = 0;
    if (!ScanTimeSecond(str, s + hour_len + minute_len, r, &second_len)) {
      // TimeHour TimeMinute
      *out_length = hour_len + minute_len;
      return true;
    }
    int32_t fraction_len = 0;
    ScanTimeFraction(str, s + hour_len + minute_len + second_len, r,
                     &fraction_len);
    // TimeHour TimeMinute TimeSecond [TimeFraction]
    *out_length = hour_len + minute_len + second_len + fraction_len;
    return true;
  }
}

// TimeSpecSeparator: DateTimeSeparator TimeSpec
// DateTimeSeparator: SPACE, 't', or 'T'
template <typename Char>
bool ScanTimeSpecSeparator(base::Vector<Char> str, int32_t s, ParsedResult* r,
                           int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (!(((s + 1) < str.length()) && IS_DATE_TIME_SEPARATOR(str[s])))
    return false;
  int32_t len = 0;
  if (!ScanTimeSpec(str, s + 1, r, &len)) return false;
  *out_length = 1 + len;
  return true;
}

// DateExtendedYear: Sign Digit Digit Digit Digit Digit Digit
template <typename Char>
bool ScanDateExtendedYear(base::Vector<Char> str, int32_t s,
                          std::string* out_year, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 7)) {
    return false;
  }
  if (IS_SIGN(str[s]) && IS_DIGIT(str[s + 1]) && IS_DIGIT(str[s + 2]) &&
      IS_DIGIT(str[s + 3]) && IS_DIGIT(str[s + 4]) && IS_DIGIT(str[s + 5]) &&
      IS_DIGIT(str[s + 6])) {
    *out_length = 7;
    (*out_year) += (IS_MINUS_SIGN(str[s]) ? '-' : str[s]);
    (*out_year) += str[s + 1];
    (*out_year) += str[s + 2];
    (*out_year) += str[s + 3];
    (*out_year) += str[s + 4];
    (*out_year) += str[s + 5];
    (*out_year) += str[s + 6];
    return true;
  }
  return false;
}

// DateFourDigitYear: Digit Digit Digit Digit
template <typename Char>
bool ScanDateFourDigitYear(base::Vector<Char> str, int32_t s,
                           std::string* out_year, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 4)) return false;
  if (IS_DIGIT(str[s]) && IS_DIGIT(str[s + 1]) && IS_DIGIT(str[s + 2]) &&
      IS_DIGIT(str[s + 3])) {
    *out_length = 4;
    (*out_year) += str[s];
    (*out_year) += str[s + 1];
    (*out_year) += str[s + 2];
    (*out_year) += str[s + 3];
    return true;
  }
  return false;
}

// DateYear:
//   DateFourDigitYear
//   DateExtendedYear
SCAN_EITHER_FORWARD_TO_FIELD(DateYear, DateFourDigitYear, DateExtendedYear,
                             date_year, ParsedResult)

// DateMonth:
//   0 NonzeroDigit
//   10
//   11
//   12
template <typename Char>
bool ScanDateMonth(base::Vector<Char> str, int32_t s, ParsedResult* r,
                   int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 2)) return false;
  if (((str[s] == '0') && IS_NON_ZERO_DIGIT(str[s + 1])) ||
      ((str[s] == '1') && IN_RANGE('0', str[s + 1], '2'))) {
    *out_length = 2;
    r->date_month = str[s];
    r->date_month += str[s + 1];
    return true;
  }
  return false;
}

// DateDay:
//   0 NonzeroDigit
//   1 Digit
//   2 Digit
//   30
//   31
template <typename Char>
bool ScanDateDay(base::Vector<Char> str, int32_t s, ParsedResult* r,
                 int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 2)) return false;
  if (((str[s] == '0') && IS_NON_ZERO_DIGIT(str[s + 1])) ||
      (IN_RANGE('1', str[s], '2') && IS_DIGIT(str[s + 1])) ||
      ((str[s] == '3') && IN_RANGE('0', str[s + 1], '1'))) {
    *out_length = 2;
    r->date_day = str[s];
    r->date_day += str[s + 1];
    return true;
  }
  return false;
}

// Date:
//   DateYear - DateMonth - DateDay
//   DateYear DateMonth DateDay
template <typename Char>
bool ScanDate(base::Vector<Char> str, int32_t s, ParsedResult* r,
              int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  int32_t year_len = 0;
  if (!ScanDateYear(str, s, r, &year_len)) {
    return false;
  }
  if ((s + year_len) == str.length()) {
    return false;
  }
  if (str[s + year_len] == '-') {
    int32_t month_len;
    if (!ScanDateMonth(str, s + year_len + 1, r, &month_len)) {
      r->date_year.clear();
      return false;
    }
    if (((s + year_len + 1 + month_len) == str.length()) ||
        (str[s + year_len + 1 + month_len] != '-')) {
      r->date_year.clear();
      r->date_month.clear();
      return false;
    }
    int32_t day_len;
    if (!ScanDateDay(str, s + year_len + 1 + month_len + 1, r, &day_len)) {
      r->date_year.clear();
      r->date_month.clear();
      return false;
    }
    // DateYear - DateMonth - DateDay
    *out_length = year_len + 1 + month_len + 1 + day_len;
    return true;
  } else {
    int32_t month_len;
    if (!ScanDateMonth(str, s + year_len, r, &month_len)) {
      r->date_year.clear();
      return false;
    }
    int32_t day_len;
    if (!ScanDateDay(str, s + year_len + month_len, r, &day_len)) {
      r->date_year.clear();
      r->date_month.clear();
      return false;
    }
    // DateYear DateMonth DateDay
    *out_length = year_len + month_len + day_len;
    return true;
  }
}

// TimeZoneUTCOffsetHour: Hour
SCAN_FORWARD_TO_FIELD(TimeZoneUTCOffsetHour, Hour, tzuo_hour, ParsedResult)

// TimeZoneUTCOffsetMinute
SCAN_FORWARD_TO_FIELD(TimeZoneUTCOffsetMinute, MinuteSecond, tzuo_minute,
                      ParsedResult)

// TimeZoneUTCOffsetSecond
SCAN_FORWARD_TO_FIELD(TimeZoneUTCOffsetSecond, MinuteSecond, tzuo_second,
                      ParsedResult)

// TimeZoneUTCOffsetFractionalPart: FractionalPart
// See PR1796
SCAN_FORWARD_TO_FIELD(TimeZoneUTCOffsetFractionalPart, FractionalPart,
                      tzuo_fractional_part, ParsedResult)

// TimeZoneUTCOffsetFraction: DecimalSeparator TimeZoneUTCOffsetFractionalPart
// See PR1796
template <typename Char>
bool ScanTimeZoneUTCOffsetFraction(base::Vector<Char> str, int32_t s,
                                   ParsedResult* r, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 2)) return false;
  if (!(IS_DECIMAL_SEPARATOR(str[s]))) return false;
  if (!ScanTimeZoneUTCOffsetFractionalPart(str, s + 1, r, out_length))
    return false;
  *out_length += 1;
  return true;
}

// Note: "TimeZoneUTCOffset" is abbreviated as "TZUO" below
// TimeZoneNumericUTCOffset:
//   TZUOSign TZUOHour
//   TZUOSign TZUOHour : TZUOMinute
//   TZUOSign TZUOHour : TZUOMinute : TZUOSecond [TZUOFraction]
//   TZUOSign TZUOHour TZUOMinute
//   TZUOSign TZUOHour TZUOMinute TZUOSecond [TZUOFraction]
template <typename Char>
bool ScanTimeZoneNumericUTCOffset(base::Vector<Char> str, int32_t s,
                                  ParsedResult* r, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 1)) return false;
  if (!IS_TIME_ZONE_UTC_OFFSET_SIGN(str[s])) return false;
  std::string sign({CANONICAL_SIGN(str[s])});
  int32_t sign_len = 1;
  int32_t hour_len;
  if (!ScanTimeZoneUTCOffsetHour(str, s + sign_len, r, &hour_len)) return false;
  if ((s + sign_len + hour_len) == str.length()) {
    r->tzuo_sign = sign;
    *out_length = sign_len + hour_len;
    // TZUOSign TZUOHour
    return true;
  }
  if (str[s + sign_len + hour_len] == ':') {
    int32_t minute_len;
    if (!ScanTimeZoneUTCOffsetMinute(str, s + sign_len + hour_len + 1, r,
                                     &minute_len)) {
      r->tzuo_hour.clear();
      return false;
    }
    if ((s + sign_len + hour_len + 1 + minute_len) == str.length()) {
      // TZUOSign TZUOHour : TZUOMinute
      r->tzuo_sign = sign;
      *out_length = sign_len + hour_len + 1 + minute_len;
      return true;
    }
    if (str[s + sign_len + hour_len + 1 + minute_len] != ':') {
      r->tzuo_sign = sign;
      *out_length = sign_len + hour_len + 1 + minute_len;
      return true;
    }
    int32_t second_len;
    if (!ScanTimeZoneUTCOffsetSecond(
            str, s + sign_len + hour_len + 1 + minute_len + 1, r,
            &second_len)) {
      r->tzuo_hour.clear();
      r->tzuo_minute.clear();
      return false;
    }
    int32_t fraction_len = 0;
    ScanTimeZoneUTCOffsetFraction(
        str, s + sign_len + hour_len + 1 + minute_len + 1 + second_len, r,
        &fraction_len);
    // TZUOSign TZUOHour : TZUOMinute : TZUOSecond [TZUOFraction]
    r->tzuo_sign = sign;
    *out_length =
        sign_len + hour_len + 1 + minute_len + 1 + second_len + fraction_len;
    return true;
  } else {
    int32_t minute_len;
    if (!ScanTimeZoneUTCOffsetMinute(str, s + sign_len + hour_len, r,
                                     &minute_len)) {
      // TZUOSign TZUOHour
      r->tzuo_sign = sign;
      *out_length = sign_len + hour_len;
      return true;
    }
    int32_t second_len;
    if (!ScanTimeZoneUTCOffsetSecond(str, s + sign_len + hour_len + minute_len,
                                     r, &second_len)) {
      // TZUOSign TZUOHour TZUOMinute
      r->tzuo_sign = sign;
      *out_length = 1 + hour_len + minute_len;
      return true;
    }
    int32_t fraction_len = 0;
    ScanTimeZoneUTCOffsetFraction(
        str, s + sign_len + hour_len + minute_len + second_len, r,
        &fraction_len);
    // TZUOSign TZUOHour TZUOMinute TZUOSecond [TZUOFraction]
    r->tzuo_sign = sign;
    *out_length = sign_len + hour_len + minute_len + second_len + fraction_len;
    return true;
  }
}

// TimeZoneUTCOffset:
//   TimeZoneNumericUTCOffset
//   UTCDesignator
template <typename Char>
bool ScanTimeZoneUTCOffset(base::Vector<Char> str, int32_t s, ParsedResult* r,
                           int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 1)) return false;
  if (IS_UTC_DESIGNATOR(str[s])) {
    // UTCDesignator
    *out_length = 1;
    r->utc_designator = str[s];
    return true;
  }
  // TimeZoneNumericUTCOffset
  return ScanTimeZoneNumericUTCOffset(str, s, r, out_length);
}

// TimeZoneIANANameComponent
//   TZLeadingChar TZChar{0,13} but not one of . or ..
template <typename Char>
bool ScanTimeZoneIANANameComponent(base::Vector<Char> str, int32_t s,
                                   std::string* out, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 1)) return false;
  if (!IS_TZ_LEADING_CHAR(str[s])) return false;
  // Not '.'
  if (((s + 1) == str.length()) && (str[s] == '.')) return false;
  // Not '..'
  if (((s + 2) == str.length()) && (str[s] == '.') && (str[s + 1] == '.'))
    return false;
  *out += str[s];
  int32_t len = 1;
  while (((s + len) < str.length()) && (len < 14) && IS_TZ_CHAR(str[s + len])) {
    *out += str[s + len];
    len++;
  }
  *out_length = len;
  return true;
}

// TimeZoneIANAName
//   TimeZoneIANANameComponent
//   TimeZoneIANANameComponent / TimeZoneIANAName
template <typename Char>
bool ScanTimeZoneIANAName(base::Vector<Char> str, int32_t s, std::string* out,
                          int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  int32_t len1;
  if (!ScanTimeZoneIANANameComponent(str, s, out, &len1)) {
    out->clear();
    *out_length = 0;
    return false;
  }
  if ((str.length() < (s + len1 + 2)) || (str[s + len1] != '/')) {
    // TimeZoneIANANameComponent
    *out_length = len1;
    return true;
  }
  std::string part2;
  int32_t len2;
  if (!ScanTimeZoneIANANameComponent(str, s + len1 + 1, &part2, &len2)) {
    out->clear();
    *out_length = 0;
    return false;
  }
  // TimeZoneIANANameComponent / TimeZoneIANAName
  *out += '/' + part2;
  *out_length = len1 + 1 + len2;
  return true;
}

template <typename Char>
bool ScanTimeZoneIANAName(base::Vector<Char> str, int32_t s, ParsedResult* r,
                          int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  return ScanTimeZoneIANAName(str, s, &(r->tzi_name), out_length);
}

// TimeZoneUTCOffsetName
//   Sign Hour
//   Sign Hour : MinuteSecond
//   Sign Hour MinuteSecond
//   Sign Hour : MinuteSecond : MinuteSecond [Fraction]
//   Sign Hour MinuteSecond MinuteSecond [Fraction]
//
template <typename Char>
bool ScanTimeZoneUTCOffsetName(base::Vector<Char> str, int32_t s,
                               std::string* out, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 1)) return false;
  if (!IS_SIGN(str[s])) return false;
  std::string sign({CANONICAL_SIGN(str[s])});
  int32_t sign_len = 1;
  int32_t hour_len = 0;
  std::string hour;
  if (!ScanHour(str, s + sign_len, &hour, &hour_len)) return false;
  if ((s + sign_len + hour_len) == str.length()) {
    // Sign Hour
    *out = sign + hour;
    *out_length = sign_len + hour_len;
    return true;
  }
  if (str[s + sign_len + hour_len] == ':') {
    int32_t minute_len = 0;
    std::string minute;
    if (!ScanMinuteSecond(str, s + sign_len + hour_len + 1, &minute,
                          &minute_len))
      return false;
    if ((s + sign_len + hour_len + 1 + minute_len) == str.length() ||
        (str[s + sign_len + hour_len + 1 + minute_len] != ':')) {
      // Sign Hour : MinuteSecond
      *out = sign + hour + ":" + minute;
      *out_length = sign_len + hour_len + 1 + minute_len;
      return true;
    }
    int32_t second_len = 0;
    std::string second;
    if (!ScanMinuteSecond(str, s + sign_len + hour_len + 1 + minute_len + 1,
                          &second, &second_len))
      return false;
    int32_t fraction_len = 0;
    std::string fraction;
    // TODO(ftang) Problem See Issue 1794
    // ScanFraction(str, s + hour_len + 1 + minute_len + 1 + second_len, ??,
    // &fraction_len);
    // Sign Hour : MinuteSecond : MinuteSecond [Fraction]
    *out = sign + hour + ":" + minute + ":" + second + fraction;
    *out_length =
        sign_len + hour_len + 1 + minute_len + 1 + second_len + fraction_len;
    return true;
  } else {
    int32_t minute_len = 0;
    std::string minute;
    if (!ScanMinuteSecond(str, s + hour_len, &minute, &minute_len)) {
      // Sign Hour
      *out = sign + hour;
      *out_length = sign_len + hour_len;
      return true;
    }
    int32_t second_len = 0;
    std::string second;
    if (!ScanMinuteSecond(str, s + hour_len + minute_len, &second,
                          &second_len)) {
      // Sign Hour MinuteSecond
      *out = sign + hour + minute;
      *out_length = sign_len + hour_len + minute_len;
      return true;
    }
    int32_t fraction_len = 0;
    std::string fraction;
    // TODO(ftang) Problem See Issue 1794
    // ScanFraction(str, s + hour_len + minute_len + second_len, ??,
    // &fraction_len); Sign Hour MinuteSecond MinuteSecond [Fraction]
    *out = sign + hour + minute + second + fraction;
    *out_length = sign_len + hour_len + minute_len + second_len + fraction_len;
    return true;
  }
}

// TimeZoneBrackedName
//   TimeZoneIANAName
//   "Etc/GMT" ASCIISign Hour
//   TimeZoneUTCOffsetName
template <typename Char>
bool ScanTimeZoneBrackedName(base::Vector<Char> str, int32_t s, ParsedResult* r,
                             int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (ScanTimeZoneIANAName(str, s, &(r->tzi_name), out_length)) {
    // TimeZoneIANAName
    return true;
  }
  if (ScanTimeZoneUTCOffsetName(str, s, &(r->tzi_name), out_length)) {
    // TimeZoneUTCOffsetName
    return true;
  }
  if ((s + 10) != str.length()) return false;
  if ((str[s] != 'E') || (str[s + 1] != 't') || (str[s + 2] != 'c') ||
      (str[s + 3] != '/') || (str[s + 4] != 'G') || (str[s + 5] != 'M') ||
      (str[s + 6] != 'T') || IS_ASCII_SIGN(str[s + 7]))
    return false;
  if (!ScanHour(str, s + 8, &(r->tzi_name), out_length)) return false;
  //   "Etc/GMT" ASCIISign Hour
  std::string etc_gmt = "Etc/GMT";
  etc_gmt += static_cast<char>(str[s + 7]);
  r->tzi_name = etc_gmt + r->tzi_name;
  *out_length += etc_gmt.length();
  return true;
}

// TimeZoneBrackedAnnotation: '[' TimeZoneBrackedName ']'
template <typename Char>
bool ScanTimeZoneBrackedAnnotation(base::Vector<Char> str, int32_t s,
                                   ParsedResult* r, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 3)) return false;
  if (str[s] != '[') return false;
  if (!ScanTimeZoneBrackedName(str, s + 1, r, out_length)) return false;
  if (str[s + (*out_length) + 1] != ']') return false;
  *out_length += 2;
  return true;
}

// TimeZoneOffsetRequired:
//   TimeZoneUTCOffset [TimeZoneBrackedAnnotation]
template <typename Char>
bool ScanTimeZoneOffsetRequired(base::Vector<Char> str, int32_t s,
                                ParsedResult* r, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  int32_t len1;
  if (!ScanTimeZoneUTCOffset(str, s, r, &len1)) return false;
  int32_t len2 = 0;
  ScanTimeZoneBrackedAnnotation(str, s + len1, r, &len2);
  *out_length = len1 + len2;
  return true;
}

//   TimeZoneNameRequired:
//   [TimeZoneUTCOffset] TimeZoneBrackedAnnotation
template <typename Char>
bool ScanTimeZoneNameRequired(base::Vector<Char> str, int32_t s,
                              ParsedResult* r, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  int32_t len1 = 0;
  ScanTimeZoneUTCOffset(str, s, r, &len1);
  int32_t len2;
  if (!ScanTimeZoneBrackedAnnotation(str, s + len1, r, &len2)) return false;
  *out_length = len1 + len2;
  return true;
}

// TimeZone:
//   TimeZoneOffsetRequired
//   TimeZoneNameRequired
SCAN_EITHER_FORWARD(TimeZone, TimeZoneOffsetRequired, TimeZoneNameRequired,
                    ParsedResult)

// Time: TimeSpec [TimeZone]
template <typename Char>
bool ScanTime(base::Vector<Char> str, int32_t s, ParsedResult* r,
              int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (!ScanTimeSpec(str, s, r, out_length)) {
    return false;
  }
  int32_t time_zone_len = 0;
  ScanTimeZone(str, s, r, &time_zone_len);
  *out_length += time_zone_len;
  return true;
}

// DateTime: Date [TimeSpecSeparator][TimeZone]
template <typename Char>
bool ScanDateTime(base::Vector<Char> str, int32_t s, ParsedResult* r,
                  int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  int32_t len1 = 0;
  if (!ScanDate(str, s, r, &len1)) return false;
  int32_t len2 = 0;
  ScanTimeSpecSeparator(str, s + len1, r, &len2);
  int32_t len3 = 0;
  ScanTimeZone(str, s + len1 + len2, r, &len3);
  *out_length = len1 + len2 + len3;
  return true;
}

// DateSpecYearMonth: DateYear ['-'] DateMonth
template <typename Char>
bool ScanDateSpecYearMonth(base::Vector<Char> str, int32_t s, ParsedResult* r,
                           int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  int32_t year_len;
  if (!ScanDateYear(str, s, r, &year_len)) return false;
  int32_t sep_len = (str[s + year_len] == '-') ? 1 : 0;
  int32_t month_len;
  if (!ScanDateMonth(str, s + year_len + sep_len, r, &month_len)) {
    r->date_year.clear();
    return false;
  }
  *out_length = year_len + sep_len + month_len;
  return true;
}

// DateSpecMonthDay:
//  --opt DateMonth -opt DateDay
template <typename Char>
bool ScanDateSpecMonthDay(base::Vector<Char> str, int32_t s, ParsedResult* r,
                          int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 4)) return false;
  int32_t prefix_len = 0;
  if (str[s] == '-') {
    // The first two dash are optional together
    if (str[s + 1] != '-') return false;
    prefix_len = 2;
  }
  int32_t month_len;
  if (!ScanDateMonth(str, s + prefix_len, r, &month_len)) return false;
  if (str.length() < (s + prefix_len + month_len)) return false;
  int32_t delim_len = (str[s + prefix_len + month_len] == '-') ? 1 : 0;
  int32_t day_len;
  if (!ScanDateDay(str, s + prefix_len + month_len + delim_len, r, &day_len)) {
    r->date_month.clear();
    return false;
  }
  *out_length = prefix_len + month_len + delim_len + day_len;
  return true;
}

// CalendarNameComponent:
//   CalChar CalChar CalChar [CalChar CalChar CalChar CalChar CalChar]
template <typename Char>
bool ScanCalendarNameComponent(base::Vector<Char> str, int32_t s,
                               std::string* out, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 3)) {
    *out_length = 0;
    return false;
  }
  if (!(IS_CAL_CHAR(str[s]) && IS_CAL_CHAR(str[s + 1]) &&
        IS_CAL_CHAR(str[s + 2]))) {
    *out_length = 0;
    return false;
  }
  (*out) += str[s];
  (*out) += str[s + 1];
  (*out) += str[s + 2];
  int32_t length = 3;

  while ((length < str.length()) && (length < 8) &&
         IS_CAL_CHAR(str[s + length])) {
    (*out) += str[s + length];
    length++;
  }
  *out_length = length;
  return true;
}

// CalendarName:
//   CalendarNameComponent
//   CalendarNameComponent - CalendarName
template <typename Char>
bool ScanCalendarName(base::Vector<Char> str, int32_t s, ParsedResult* r,
                      int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  int32_t len1;
  if (!ScanCalendarNameComponent(str, s, &(r->calendar_name), &len1)) {
    r->calendar_name.clear();
    return false;
  }
  if ((str.length() < (s + len1 + 1)) || (str[s + len1] != '-')) {
    // CalendarNameComponent
    *out_length = len1;
    return true;
  }
  r->calendar_name += '-';
  int32_t len2;
  if (!ScanCalendarName(str, s + len1 + 1, r, &len2)) {
    r->calendar_name.clear();
    *out_length = 0;
    return false;
  }
  // CalendarNameComponent - CalendarName
  *out_length = len1 + 1 + len2;
  return true;
}

// Calendar: '[u-ca=' CalendarName ']'
template <typename Char>
bool ScanCalendar(base::Vector<Char> str, int32_t s, ParsedResult* r,
                  int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 7)) return false;
  int32_t cur = s;
  // "[u-ca="
  if ((str[cur++] != '[') || (str[cur++] != 'u') || (str[cur++] != '-') ||
      (str[cur++] != 'c') || (str[cur++] != 'a') || (str[cur++] != '=')) {
    return false;
  }
  int32_t calendar_name_len;
  if (!ScanCalendarName(str, s + 6, r, &calendar_name_len)) {
    return false;
  }
  if ((str.length() < (s + 6 + calendar_name_len + 1)) ||
      (str[cur + calendar_name_len] != ']')) {
    return false;
  }
  *out_length = 6 + calendar_name_len + 1;
  return true;
}

// TemporalTimeZoneIdentifier:
//   TimeZoneNumericUTCOffset
//   TimeZoneIANAName
template <typename Char>
bool ScanTemporalTimeZoneIdentifier(base::Vector<Char> str, int32_t s,
                                    ParsedResult* r, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  return ScanTimeZoneNumericUTCOffset(str, s, r, out_length) ||
         ScanTimeZoneIANAName(str, s, &(r->tzi_name), out_length);
}

// CalendarDateTime: DateTime [Calendar]
template <typename Char>
bool ScanCalendarDateTime(base::Vector<Char> str, int32_t s, ParsedResult* r,
                          int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  int32_t date_time_len = 0;
  if (!ScanDateTime(str, 0, r, &date_time_len)) return false;
  int32_t calendar_len = 0;
  ScanCalendar(str, date_time_len, r, &calendar_len);
  *out_length = date_time_len + calendar_len;
  return true;
}

// TemporalZonedDateTimeString:
//   Date [TimeSpecSeparator] TimeZoneNameRequired [Calendar]
template <typename Char>
bool ScanTemporalZonedDateTimeString(base::Vector<Char> str, int32_t s,
                                     ParsedResult* r, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  // Date
  int32_t date_len = 0;
  if (!ScanDate(str, s, r, &date_len)) {
    return false;
  }

  // TimeSpecSeparator
  int32_t time_spec_separator_len = 0;
  ScanTimeSpecSeparator(str, s + date_len, r, &time_spec_separator_len);

  // TimeZoneNameRequired
  int32_t time_zone_name_len = 0;
  if (!ScanTimeZoneNameRequired(str, s + date_len + time_spec_separator_len, r,
                                &time_zone_name_len)) {
    return false;
  }

  // Calendar
  int32_t calendar_len = 0;
  ScanCalendar(str, s + date_len + time_spec_separator_len + time_zone_name_len,
               r, &calendar_len);
  *out_length =
      date_len + time_spec_separator_len + time_zone_name_len + calendar_len;
  return true;
}

SCAN_FORWARD(TemporalDateString, CalendarDateTime, ParsedResult)
SCAN_FORWARD(TemporalDateTimeString, CalendarDateTime, ParsedResult)

// TemporalTimeZoneString:
//   TemporalTimeZoneIdentifier
//   Date [TimeSpecSeparator] TimeZone [Calendar]
template <typename Char>
bool ScanDate_TimeSpecSeparator_TimeZone_Calendar(base::Vector<Char> str,
                                                  int32_t s, ParsedResult* r,
                                                  int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  int32_t date_len = 0;
  if (!ScanDate(str, s, r, &date_len)) return false;
  int32_t time_spec_len = 0;
  ScanTimeSpecSeparator(str, s + date_len, r, &time_spec_len);
  int32_t time_zone_len = 0;
  if (!ScanTimeZone(str, s + date_len + time_spec_len, r, &time_zone_len))
    return false;
  int32_t calendar_len = 0;
  ScanCalendar(str, s + date_len + time_spec_len + time_zone_len, r,
               &calendar_len);
  *out_length = date_len + time_spec_len + time_zone_len + calendar_len;
  return true;
}
SCAN_EITHER_FORWARD(TemporalTimeZoneString, TemporalTimeZoneIdentifier,
                    Date_TimeSpecSeparator_TimeZone_Calendar, ParsedResult)

//   Time
//   DateTime
SCAN_EITHER_FORWARD(TemporalTimeString, Time, DateTime, ParsedResult)

// TemporalYearMonthString:
//   DateSpecYearMonth
//   DateTime
SCAN_EITHER_FORWARD(TemporalYearMonthString, DateSpecYearMonth, DateTime,
                    ParsedResult)

// TemporalMonthDayString
//   DateSpecMonthDay
//   DateTime
SCAN_EITHER_FORWARD(TemporalMonthDayString, DateSpecMonthDay, DateTime,
                    ParsedResult)

// TemporalRelativeToString:
//   TemporalDateTimeString
//   TemporalZonedDateTimeString
SCAN_EITHER_FORWARD(TemporalRelativeToString, TemporalDateTimeString,
                    TemporalZonedDateTimeString, ParsedResult)

// TemporalInstantString
//   Date TimeZoneOffsetRequired
//   Date DateTimeSeparator TimeSpec TimeZoneOffsetRequired
template <typename Char>
bool ScanTemporalInstantString(base::Vector<Char> str, int32_t s,
                               ParsedResult* r, int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  // Date
  int32_t date_len = 0;
  if (!ScanDate(str, s, r, &date_len)) return false;

  // TimeZoneOffsetRequired
  int32_t time_zone_offset_len;
  if (ScanTimeZoneOffsetRequired(str, s + date_len, r, &time_zone_offset_len)) {
    *out_length = date_len + time_zone_offset_len;
    return true;
  }

  // DateTimeSeparator
  if (!((date_len < str.length()) && IS_DATE_TIME_SEPARATOR(str[date_len]))) {
    return false;
  }
  int32_t date_time_separator_len = 1;

  // TimeSpec
  int32_t time_spec_len;
  if (!ScanTimeSpec(str, date_len + date_time_separator_len, r,
                    &time_spec_len)) {
    return false;
  }

  // TimeZoneOffsetRequired
  if (!ScanTimeZoneOffsetRequired(
          str, date_len + date_time_separator_len + time_spec_len, r,
          &time_zone_offset_len)) {
    return false;
  }
  *out_length =
      date_len + date_time_separator_len + time_spec_len + time_zone_offset_len;
  return true;
}

// TemporalCalendarString:
//   CalendarName
//   TemporalInstantString
//   CalendarDateTime
//   Time
//   DateSpecYearMonth
//   DateSpecMonthDay
template <typename Char>
bool ScanTemporalCalendarString(base::Vector<Char> str, int32_t s,
                                ParsedResult* r, int32_t* len) {
  TEMPORAL_ENTER_FUNC

  return ScanCalendarName(str, s, r, len) ||
         ScanTemporalInstantString(str, s, r, len) ||
         ScanCalendarDateTime(str, s, r, len) || ScanTime(str, s, r, len) ||
         ScanDateSpecYearMonth(str, s, r, len) ||
         ScanDateSpecMonthDay(str, s, r, len);
}

// ==============================================================================
#define SATISIFY(T, R)                                                  \
  template <typename Char>                                              \
  bool Satisfy##T(base::Vector<Char> str, R* r) {                       \
    TEMPORAL_ENTER_FUNC                                                 \
    int32_t len;                                                        \
    if (Scan##T(str, 0, r, &len) && (len == str.length())) return true; \
    r->clear();                                                         \
    return false;                                                       \
  }

#define IF_SATISFY_RETURN(T)             \
  {                                      \
    if (Satisfy##T(str, r)) return true; \
  }

#define SATISIFY_EITHER(T1, T2, T3, R)             \
  template <typename Char>                         \
  bool Satisfy##T1(base::Vector<Char> str, R* r) { \
    TEMPORAL_ENTER_FUNC                            \
    IF_SATISFY_RETURN(T2)                          \
    IF_SATISFY_RETURN(T3)                          \
    return false;                                  \
  }

SATISIFY(TemporalDateTimeString, ParsedResult)
SATISIFY(TemporalDateString, ParsedResult)
SATISIFY(Time, ParsedResult)
SATISIFY(DateTime, ParsedResult)
SATISIFY(DateSpecYearMonth, ParsedResult)
SATISIFY(DateSpecMonthDay, ParsedResult)
SATISIFY(Date_TimeSpecSeparator_TimeZone_Calendar, ParsedResult)
SATISIFY_EITHER(TemporalTimeString, Time, DateTime, ParsedResult)
SATISIFY_EITHER(TemporalYearMonthString, DateSpecYearMonth, DateTime,
                ParsedResult)
SATISIFY_EITHER(TemporalMonthDayString, DateSpecMonthDay, DateTime,
                ParsedResult)
SATISIFY(TimeZoneNumericUTCOffset, ParsedResult)
SATISIFY(TimeZoneIANAName, ParsedResult)
SATISIFY_EITHER(TemporalTimeZoneIdentifier, TimeZoneNumericUTCOffset,
                TimeZoneIANAName, ParsedResult)
SATISIFY_EITHER(TemporalTimeZoneString, TemporalTimeZoneIdentifier,
                Date_TimeSpecSeparator_TimeZone_Calendar, ParsedResult)
SATISIFY(TemporalInstantString, ParsedResult)
SATISIFY(TemporalZonedDateTimeString, ParsedResult)

SATISIFY_EITHER(TemporalRelativeToString, TemporalDateTimeString,
                TemporalZonedDateTimeString, ParsedResult)

SATISIFY(CalendarName, ParsedResult)
SATISIFY(CalendarDateTime, ParsedResult)

template <typename Char>
bool SatisfyTemporalCalendarString(base::Vector<Char> str, ParsedResult* r) {
  TEMPORAL_ENTER_FUNC

  IF_SATISFY_RETURN(CalendarName)
  IF_SATISFY_RETURN(TemporalInstantString)
  IF_SATISFY_RETURN(CalendarDateTime)
  IF_SATISFY_RETURN(Time)
  IF_SATISFY_RETURN(DateSpecYearMonth)
  IF_SATISFY_RETURN(DateSpecMonthDay)
  return false;
}

// Duration
//

SCAN_FORWARD(TimeFractionalPart, FractionalPart, std::string)

template <typename Char>
bool ScanFraction(base::Vector<Char> str, int32_t s, std::string* out,
                  int32_t* out_length) {
  TEMPORAL_ENTER_FUNC

  if (str.length() < (s + 2)) return false;
  if (!(IS_DECIMAL_SEPARATOR(str[s]))) return false;
  std::string part;
  if (!ScanTimeFractionalPart(str, s + 1, &part, out_length)) return false;
  *out = str[s];
  *out += part;
  *out_length += 1;
  return true;
}

SCAN_FORWARD(TimeFraction, Fraction, std::string)

// Digits :
//   Digit [Digits]
template <typename Char>
bool ScanDigits(base::Vector<Char> str, int32_t s, std::string* out,
                int32_t* len) {
  TEMPORAL_ENTER_FUNC

  int32_t l = 0;
  if (str.length() < (s + 1)) return false;
  if (!IS_DIGIT(str[s])) return false;
  *out = str[s];
  l++;
  while (s + l + 1 <= str.length() && IS_DIGIT(str[s + l])) {
    *out += str[s + l];
    l++;
  }
  *len = l;
  return true;
}

SCAN_FORWARD_TO_FIELD(DurationYears, Digits, years, ParsedDuration)
SCAN_FORWARD_TO_FIELD(DurationMonths, Digits, months, ParsedDuration)
SCAN_FORWARD_TO_FIELD(DurationWeeks, Digits, weeks, ParsedDuration)
SCAN_FORWARD_TO_FIELD(DurationDays, Digits, days, ParsedDuration)

// DurationWholeHours :
//  Digits
SCAN_FORWARD_TO_FIELD(DurationWholeHours, Digits, whole_hours, ParsedDuration)
// DurationWholeMinutes :
//  Digits
SCAN_FORWARD_TO_FIELD(DurationWholeMinutes, Digits, whole_minutes,
                      ParsedDuration)
// DurationWholeSeconds :
//  Digits
SCAN_FORWARD_TO_FIELD(DurationWholeSeconds, Digits, whole_seconds,
                      ParsedDuration)

// DurationHoursFraction :
//   TimeFraction
SCAN_FORWARD_TO_FIELD(DurationHoursFraction, TimeFraction, hours_fraction,
                      ParsedDuration)
// DurationMinutesFraction :
//   TimeFraction
SCAN_FORWARD_TO_FIELD(DurationMinutesFraction, TimeFraction, minutes_fraction,
                      ParsedDuration)
// DurationSecondsFraction :
//   TimeFraction
SCAN_FORWARD_TO_FIELD(DurationSecondsFraction, TimeFraction, seconds_fraction,
                      ParsedDuration)

// DurationSecondsPart :
//   DurationWholeSeconds DurationSecondsFractionopt SecondsDesignator
template <typename Char>
bool ScanDurationSecondsPart(base::Vector<Char> str, int32_t s,
                             ParsedDuration* r, int32_t* len) {
  TEMPORAL_ENTER_FUNC

  int32_t first_len = 0;
  if (!ScanDurationWholeSeconds(str, s, r, &first_len)) return false;
  int32_t second_len = 0;
  ScanDurationSecondsFraction(str, s + first_len, r, &second_len);
  if (str.length() < (s + first_len + second_len + 1)) return false;
  if (!IS_SECONDS_DESIGNATOR(str[s + first_len + second_len])) return false;
  *len = first_len + second_len + 1;
  return true;
}
// DurationMinutesPart :
//   DurationWholeMinutes DurationMinutesFractionopt MinutesDesignator
//   [DurationSecondsPart]
template <typename Char>
bool ScanDurationMinutesPart(base::Vector<Char> str, int32_t s,
                             ParsedDuration* r, int32_t* len) {
  TEMPORAL_ENTER_FUNC

  int32_t first_len = 0;
  if (!ScanDurationWholeMinutes(str, s, r, &first_len)) return false;
  int32_t second_len = 0;
  ScanDurationMinutesFraction(str, s + first_len, r, &second_len);
  if (str.length() < (s + first_len + second_len + 1)) return false;
  if (!IS_MINUTES_DESIGNATOR(str[s + first_len + second_len])) return false;
  int32_t part_len = 0;
  ScanDurationSecondsPart(str, s + first_len + second_len + 1, r, &part_len);
  *len = first_len + second_len + 1 + part_len;
  return true;
}
// DurationHoursPart :
//   DurationWholeHours DurationHoursFractionopt HoursDesignator
//   DurationMinutesPart
//
//   DurationWholeHours DurationHoursFractionopt HoursDesignator
//   [DurationSecondsPart]
template <typename Char>
bool ScanDurationHoursPart(base::Vector<Char> str, int32_t s, ParsedDuration* r,
                           int32_t* len) {
  TEMPORAL_ENTER_FUNC

  int32_t first_len = 0;
  if (!ScanDurationWholeHours(str, s, r, &first_len)) return false;
  int32_t second_len = 0;
  ScanDurationHoursFraction(str, s + first_len, r, &second_len);
  if (str.length() < (s + first_len + second_len + 1)) return false;
  if (!IS_HOURS_DESIGNATOR(str[s + first_len + second_len])) return false;
  int32_t part_len = 0;
  if (ScanDurationMinutesPart(str, s + first_len + second_len + 1, r,
                              &part_len)) {
    *len = first_len + second_len + 1 + part_len;
    return true;
  }
  r->whole_minutes = r->minutes_fraction = r->whole_seconds =
      r->seconds_fraction = "";
  part_len = 0;
  ScanDurationSecondsPart(str, s + first_len + second_len + 1, r, &part_len);
  *len = first_len + second_len + 1 + part_len;
  return true;
}

// DurationTime :
//   DurationTimeDesignator DurationHoursPart
//   DurationTimeDesignator DurationMinutesPart
//   DurationTimeDesignator DurationSecondsPart
template <typename Char>
bool ScanDurationTime(base::Vector<Char> str, int32_t s, ParsedDuration* r,
                      int32_t* len) {
  TEMPORAL_ENTER_FUNC

  int32_t part_len = 0;
  if (str.length() < (s + 1)) return false;
  if (!IS_TIME_DESIGNATOR(str[s])) return false;
  do {
    if (ScanDurationHoursPart(str, s + 1, r, &part_len)) break;
    r->whole_hours = r->hours_fraction = r->whole_minutes =
        r->minutes_fraction = r->whole_seconds = r->seconds_fraction = "";

    if (ScanDurationMinutesPart(str, s + 1, r, &part_len)) break;
    r->whole_minutes = r->minutes_fraction = r->whole_seconds =
        r->seconds_fraction = "";

    if (ScanDurationSecondsPart(str, s + 1, r, &part_len)) break;
    r->whole_seconds = r->seconds_fraction = "";
    return false;
  } while (1);
  *len = 1 + part_len;
  return true;
}

// DurationDaysPart :
//   DurationDays DaysDesignator
template <typename Char>
bool ScanDurationDaysPart(base::Vector<Char> str, int32_t s, ParsedDuration* r,
                          int32_t* len) {
  TEMPORAL_ENTER_FUNC

  int32_t first_len = 0;
  if (!ScanDurationDays(str, s, r, &first_len)) return false;
  if (str.length() < (s + first_len + 1)) return false;
  if (!IS_DAYS_DESIGNATOR(str[s + first_len])) return false;
  *len = first_len + 1;
  return true;
}

// DurationWeeksPart :
//   DurationWeeks WeeksDesignator [DurationDaysPart]
template <typename Char>
bool ScanDurationWeeksPart(base::Vector<Char> str, int32_t s, ParsedDuration* r,
                           int32_t* len) {
  TEMPORAL_ENTER_FUNC

  int32_t first_len = 0;
  if (!ScanDurationWeeks(str, s, r, &first_len)) return false;
  if (str.length() < (s + first_len + 1)) return false;
  if (!IS_WEEKS_DESIGNATOR(str[s + first_len])) return false;
  int32_t second_len = 0;
  ScanDurationDaysPart(str, s + first_len + 1, r, &second_len);
  *len = first_len + 1 + second_len;
  return true;
}

// DurationMonthsPart :
//   DurationMonths MonthsDesignator DurationWeeksPart
//   DurationMonths MonthsDesignator [DurationDaysPart]
template <typename Char>
bool ScanDurationMonthsPart(base::Vector<Char> str, int32_t s,
                            ParsedDuration* r, int32_t* len) {
  TEMPORAL_ENTER_FUNC

  int32_t first_len = 0;
  if (!ScanDurationMonths(str, s, r, &first_len)) return false;

  if (str.length() < (s + first_len + 1)) return false;
  if (!IS_MONTHS_DESIGNATOR(str[s + first_len])) return false;
  int32_t second_len = 0;
  if (ScanDurationWeeksPart(str, s + first_len + 1, r, &second_len)) {
    *len = first_len + 1 + second_len;
    return true;
  }
  r->weeks = r->days = "";
  second_len = 0;
  ScanDurationDaysPart(str, s + first_len + 1, r, &second_len);
  *len = first_len + 1 + second_len;
  return true;
}

// DurationYearsPart :
//   DurationYears YearsDesignator DurationMonthsPart
//   DurationYears YearsDesignator DurationWeeksPart
//   DurationYears YearsDesignator [DurationDaysPart]
template <typename Char>
bool ScanDurationYearsPart(base::Vector<Char> str, int32_t s, ParsedDuration* r,
                           int32_t* len) {
  TEMPORAL_ENTER_FUNC

  int32_t first_len = 0;
  if (!ScanDurationYears(str, s, r, &first_len)) return false;
  if (str.length() < (s + first_len + 1)) return false;
  if (!IS_YEARS_DESIGNATOR(str[s + first_len])) return false;
  int32_t second_len = 0;
  if (ScanDurationMonthsPart(str, s + 1 + first_len, r, &second_len)) {
    *len = first_len + 1 + second_len;
    return true;
  }
  // Reset failed attempt above.
  r->months = r->weeks = r->days = "";
  if (ScanDurationWeeksPart(str, s + 1 + first_len, r, &second_len)) {
    *len = first_len + 1 + second_len;
    return true;
  }
  // Reset failed attempt above.
  r->weeks = r->days = "";
  second_len = 0;
  ScanDurationDaysPart(str, s + 1 + first_len, r, &second_len);
  *len = first_len + 1 + second_len;
  return true;
}

// DurationDate :
//   DurationYearsPart [DurationTime]
//   DurationMonthsPart [DurationTime]
//   DurationWeeksPart [DurationTime]
//   DurationDaysPart [DurationTime]
template <typename Char>
bool ScanDurationDate(base::Vector<Char> str, int32_t s, ParsedDuration* r,
                      int32_t* len) {
  TEMPORAL_ENTER_FUNC

  int32_t first_len;

  do {
    if (ScanDurationYearsPart(str, s, r, &first_len)) break;
    r->years = r->months = r->weeks = r->days = "";
    if (ScanDurationMonthsPart(str, s, r, &first_len)) break;
    r->months = r->weeks = r->days = "";
    if (ScanDurationWeeksPart(str, s, r, &first_len)) break;
    r->weeks = r->days = "";
    if (ScanDurationDaysPart(str, s, r, &first_len)) break;
    r->days = "";
    return false;
  } while (1);

  int32_t second_len = 0;
  ScanDurationTime(str, s + first_len, r, &second_len);
  *len = first_len + second_len;
  return true;
}

// Duration :
//   Signopt DurationDesignator DurationDate
//   Signopt DurationDesignator DurationTime
template <typename Char>
bool ScanDuration(base::Vector<Char> str, int32_t s, ParsedDuration* r,
                  int32_t* len) {
  TEMPORAL_ENTER_FUNC

  int32_t first_len = 0;
  if (str.length() < (s + 2)) return false;
  std::string sign;
  if (IS_SIGN(str[s])) {
    sign = {CANONICAL_SIGN(str[s])};
    first_len++;
  }
  if (!IS_DURATION_DESIGNATOR(str[s + first_len])) return false;
  int32_t second_len = 0;
  if (ScanDurationDate(str, s + first_len + 1, r, &second_len)) {
    *len = first_len + 1 + second_len;
    r->sign = sign;
    return true;
  }
  // Reset the parsed years, months, weeks, and days in the above failed
  // attempt.
  r->years = r->months = r->weeks = r->days = "";
  if (ScanDurationTime(str, s + first_len + 1, r, &second_len)) {
    *len = first_len + 1 + second_len;
    r->sign = sign;
    return true;
  }
  return false;
}
SCAN_FORWARD(TemporalDurationString, Duration, ParsedDuration)

SATISIFY(TemporalDurationString, ParsedDuration)
}  // namespace

#define SATISFY_STRING(N, parsed, valid)                                   \
  {                                                                        \
    iso_string = String::Flatten(isolate, iso_string);                     \
    {                                                                      \
      DisallowGarbageCollection no_gc;                                     \
      String::FlatContent str_content = iso_string->GetFlatContent(no_gc); \
      if (str_content.IsOneByte()) {                                       \
        valid = Satisfy##N(str_content.ToOneByteVector(), &parsed);        \
      } else {                                                             \
        valid = Satisfy##N(str_content.ToUC16Vector(), &parsed);           \
      }                                                                    \
    }                                                                      \
  }

#define SATISFY_STRING_OR_THROW(N, R, parsed)                            \
  {                                                                      \
    bool valid;                                                          \
    SATISFY_STRING(N, parsed, valid)                                     \
    if (!valid) {                                                        \
      /* a. Throw a RangeError exception. */                             \
      THROW_NEW_ERROR_RETURN_VALUE(                                      \
          isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), Nothing<R>()); \
    }                                                                    \
  }

// #sec-temporal-parsetemporaltimezonestring
Maybe<TimeZoneRecord> ParseTemporalTimeZoneString(Isolate* isolate,
                                                  Handle<String> iso_string) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalTimeZoneString
  // (see 13.33), then
  ParsedResult parsed;
  SATISFY_STRING_OR_THROW(TemporalTimeZoneString, TimeZoneRecord, parsed)
  // 3. Let z, sign, hours, minutes, seconds, fraction and name be the parts of
  // isoString produced respectively by the UTCDesignator,
  // TimeZoneUTCOffsetSign, TimeZoneUTCOffsetHour, TimeZoneUTCOffsetMinute,
  // TimeZoneUTCOffsetSecond, TimeZoneUTCOffsetFraction, and TimeZoneIANAName
  // productions, or undefined if not present.
  // 4. If z is not undefined, then
  if (!parsed.utc_designator.empty()) {
    // a. Return the Record { [[Z]]: true, [[OffsetString]]: undefined,
    // [[Name]]: name }.
    TimeZoneRecord ret({true, "", parsed.tzi_name});
    return Just(ret);
  }
  // 5. If hours is undefined, then
  // a. Let offsetString be undefined.
  // 6. Else,
  std::string offset_string;
  if (!parsed.tzuo_hour.empty()) {
    // a. Assert: sign is not undefined.
    CHECK(!parsed.tzuo_sign.empty());
    // b. Set hours to ! ToIntegerOrInfinity(hours).
    int32_t hours = StringToInt(parsed.tzuo_hour);
    // c. If sign is the code unit 0x002D (HYPHEN-MINUS) or the code unit 0x2212
    // (MINUS SIGN), then i. Set sign to −1. d. Else, i. Set sign to 1.
    int32_t sign = (parsed.tzuo_sign[0] == '-') ? -1 : 1;
    // e. Set minutes to ! ToIntegerOrInfinity(minutes).
    int32_t minutes = StringToInt(parsed.tzuo_minute);
    // f. Set seconds to ! ToIntegerOrInfinity(seconds).
    int32_t seconds = StringToInt(parsed.tzuo_second);
    // g. If fraction is not undefined, then
    int32_t nanoseconds;
    if (!parsed.tzuo_fractional_part.empty()) {
      // i. Set fraction to the string-concatenation of the previous value of
      // fraction and the string "000000000".
      std::string fraction = parsed.tzuo_fractional_part + "000000000";
      // ii. Let nanoseconds be the String value equal to the substring of
      // fraction from 0 to 9. iii. Set nanoseconds to !
      // ToIntegerOrInfinity(nanoseconds).
      nanoseconds = StringToInt(fraction.substr(0, 9));
      // h. Else,
    } else {
      // i. Let nanoseconds be 0.
      nanoseconds = 0;
    }
    // i. Let offsetNanoseconds be sign × (((hours × 60 + minutes) × 60 +
    // seconds) × 109 + nanoseconds).
    int64_t offset_nanoseconds =
        sign * ((hours * 60 + minutes) * 60 + seconds) * 1000000000L +
        nanoseconds;
    // j. Let offsetString be ! FormatTimeZoneOffsetString(offsetNanoseconds).
    offset_string = FormatTimeZoneOffsetString(offset_nanoseconds);
  }
  // 7. If name is not undefined, then
  std::string name;
  if (!parsed.tzi_name.empty()) {
    // a. If ! IsValidTimeZoneName(name) is false, throw a RangeError exception.
    if (!IsValidTimeZoneName(isolate, parsed.tzi_name)) {
      THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                   NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                                   Nothing<TimeZoneRecord>());
    }
    // b. Set name to ! CanonicalizeTimeZoneName(name).
    name = CanonicalizeTimeZoneName(isolate, parsed.tzi_name);
  }
  // 8. Return the Record { [[Z]]: false, [[OffsetString]]: offsetString,
  // [[Name]]: name }.
  TimeZoneRecord ret({false, offset_string, name});
  return Just(ret);
}

// #sec-temporal-parsetemporaltimezone
Maybe<std::string> ParseTemporalTimeZone(Isolate* isolate,
                                         Handle<String> string) {
  TEMPORAL_ENTER_FUNC

  // 2. Let result be ? ParseTemporalTimeZoneString(string).
  Maybe<TimeZoneRecord> maybe_result =
      ParseTemporalTimeZoneString(isolate, string);
  MAYBE_RETURN(maybe_result, Nothing<std::string>());
  TimeZoneRecord result = maybe_result.FromJust();

  // 3. If result.[[Name]] is not undefined, return result.[[Name]].
  if (!result.name.empty()) {
    return Just(result.name);
  }

  // 4. If result.[[Z]] is true, return "UTC".
  if (result.z) {
    return Just(std::string("UTC"));
  }

  // 5. Return result.[[OffsetString]].
  return Just(result.offset_string);
}

Maybe<int64_t> ParseTimeZoneOffsetString(Isolate* isolate,
                                         Handle<String> iso_string,
                                         bool throwIfNotSatisfy) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(offsetString) is String.
  // 2. If offsetString does not satisfy the syntax of a
  // TimeZoneNumericUTCOffset (see 13.33), then
  ParsedResult parsed;
  bool valid;
  SATISFY_STRING(TimeZoneNumericUTCOffset, parsed, valid);
  if (throwIfNotSatisfy && !valid) {
    /* a. Throw a RangeError exception. */
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                                 Nothing<int64_t>());
  }
  // 3. Let sign, hours, minutes, seconds, and fraction be the parts of
  // offsetString produced respectively by the TimeZoneUTCOffsetSign,
  // TimeZoneUTCOffsetHour, TimeZoneUTCOffsetMinute, TimeZoneUTCOffsetSecond,
  // and TimeZoneUTCOffsetFraction productions, or undefined if not present.
  // 4. If either hours or sign are undefined, throw a RangeError exception.
  if (parsed.tzuo_hour.empty() || parsed.tzuo_sign.empty()) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                                 Nothing<int64_t>());
  }
  // 5. If sign is the code unit 0x002D (HYPHEN-MINUS) or 0x2212 (MINUS SIGN),
  // then a. Set sign to −1.
  // 6. Else,
  // a. Set sign to 1.
  int64_t sign = (parsed.tzuo_sign[0] == '-') ? -1 : 1;

  // 7. Set hours to ! ToIntegerOrInfinity(hours).
  int64_t hours = StringToInt(parsed.tzuo_hour);
  // 8. Set minutes to ! ToIntegerOrInfinity(minutes).
  int64_t minutes = StringToInt(parsed.tzuo_minute);
  // 9. Set seconds to ! ToIntegerOrInfinity(seconds).
  int64_t seconds = StringToInt(parsed.tzuo_second);
  // 10. If fraction is not undefined, then
  int64_t nanoseconds;
  if (!parsed.tzuo_fractional_part.empty()) {
    // a. Set fraction to the string-concatenation of the previous value of
    // fraction and the string "000000000".
    std::string fraction = parsed.tzuo_fractional_part + "000000000";
    // b. Let nanoseconds be the String value equal to the substring of fraction
    // consisting of the code units with indices 0 (inclusive) through 9
    // (exclusive). c. Set nanoseconds to ! ToIntegerOrInfinity(nanoseconds).
    nanoseconds = StringToInt(fraction.substr(0, 9));
    // 11. Else,
  } else {
    // a. Let nanoseconds be 0.
    nanoseconds = 0;
  }
  // 12. Return sign × (((hours × 60 + minutes) × 60 + seconds) × 109 +
  // nanoseconds).
  return Just(sign * (((hours * 60 + minutes) * 60 + seconds) * 1000000000 +
                      nanoseconds));
}

bool IsValidTimeZoneNumericUTCOffsetString(Isolate* isolate,
                                           Handle<String> iso_string) {
  TEMPORAL_ENTER_FUNC

  bool valid = false;
  ParsedResult parsed;
  SATISFY_STRING(TimeZoneNumericUTCOffset, parsed, valid);
  return valid;
}

// #sec-temporal-parsetemporalcalendarstring
Maybe<std::string> ParseTemporalCalendarString(Isolate* isolate,
                                               Handle<String> iso_string) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalCalendarString
  // (see 13.33), then a. Throw a RangeError exception.
  ParsedResult parsed;
  SATISFY_STRING_OR_THROW(TemporalCalendarString, std::string, parsed)
  // 3. Let id be the part of isoString produced by the CalendarName production,
  // or undefined if not present.
  // 4. If id is undefined, then
  if (parsed.calendar_name.empty()) {
    // a. Return "iso8601".
    return Just(std::string("iso8601"));
  }
  // 5. Return id.
  return Just(parsed.calendar_name);
}
Maybe<int64_t> GetOffsetNanosecondsFor(Isolate* isolate,
                                       Handle<JSReceiver> time_zone,
                                       Handle<Object> instant) {
  // 1. Let getOffsetNanosecondsFor be ? GetMethod(timeZone,
  // "getOffsetNanosecondsFor").
  Handle<Object> get_offset_nanoseconds_for;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, get_offset_nanoseconds_for,
      Object::GetMethod(time_zone,
                        isolate->factory()->getOffsetNanosecondsFor_string()),
      Nothing<int64_t>());
  // 3. Let offsetNanoseconds be ? Call(getOffsetNanosecondsFor, timeZone, «
  // instant »).
  Handle<Object> argv[] = {instant};
  Handle<Object> offset_nanoseconds_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, offset_nanoseconds_obj,
      Execution::Call(isolate, get_offset_nanoseconds_for, time_zone, 1, argv),
      Nothing<int64_t>());

  // 4. If Type(offsetNanoseconds) is not Number, throw a TypeError exception.
  if (!offset_nanoseconds_obj->IsNumber()) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                                 Nothing<int64_t>());
  }

  // 5. If ! IsIntegralNumber(offsetNanoseconds) is false, throw a RangeError
  // exception.
  double offset_nanoseconds = offset_nanoseconds_obj->Number();
  if ((offset_nanoseconds - std::floor(offset_nanoseconds) != 0)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                                 Nothing<int64_t>());
  }

  // 6. Set offsetNanoseconds to ℝ(offsetNanoseconds).
  int64_t offset_nanoseconds_int = R(offset_nanoseconds);
  // 7. If abs(offsetNanoseconds) > 86400 × 109, throw a RangeError exception.
  if (std::abs(offset_nanoseconds_int) > 86400e9) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                                 Nothing<int64_t>());
  }
  // 8. Return offsetNanoseconds.
  return Just(offset_nanoseconds_int);
}

// #sec-temporal-getiso8601calendar
MaybeHandle<JSTemporalCalendar> GetISO8601Calendar(Isolate* isolate) {
  return CreateTemporalCalendar(isolate, isolate->factory()->iso8601_string());
}

// #sec-temporal-isbuiltincalendar
bool IsBuiltinCalendar(Isolate* isolate, Handle<String> id) {
  // 1. If id is not "iso8601", return false.
  // 2. Return true
  return isolate->factory()->iso8601_string()->Equals(*id);
}

bool IsUTC(Isolate* isolate, Handle<String> time_zone) {
  // 1. Assert: Type(timeZone) is String.
  // 2. Let tzText be ! StringToCodePoints(timeZone).
  // 3. Let tzUpperText be the result of toUppercase(tzText), according to the
  // Unicode Default Case Conversion algorithm.
  // 4. Let tzUpper be ! CodePointsToString(tzUpperText).
  // 5. If tzUpper and "UTC" are the same sequence of code points, return true.
  // 6. Return false.
  if (time_zone->length() != 3) return false;
  std::unique_ptr<char[]> p = time_zone->ToCString();
  return (p[0] == u'U' || p[0] == u'u') && (p[1] == u'T' || p[1] == u't') &&
         (p[2] == u'C' || p[2] == u'c');
}

bool IsUTC(Isolate* isolate, const std::string& time_zone) {
  if (time_zone.length() != 3) return false;
  return (time_zone[0] == u'U' || time_zone[0] == u'u') &&
         (time_zone[1] == u'T' || time_zone[1] == u't') &&
         (time_zone[2] == u'C' || time_zone[2] == u'c');
}

#ifdef V8_INTL_SUPPORT
bool IsValidTimeZoneName(Isolate* isolate, Handle<String> time_zone) {
  return IsValidTimeZoneName(isolate, time_zone->ToCString().get());
}

bool IsValidTimeZoneName(Isolate* isolate, const std::string& time_zone) {
  return Intl::IsValidTimeZoneName(isolate, time_zone);
}

Handle<String> CanonicalizeTimeZoneName(Isolate* isolate,
                                        Handle<String> identifier) {
  std::string canonicalized =
      CanonicalizeTimeZoneName(isolate, identifier->ToCString().get());
  return isolate->factory()->NewStringFromAsciiChecked(canonicalized.c_str());
}

std::string CanonicalizeTimeZoneName(Isolate* isolate,
                                     const std::string& identifier) {
  return Intl::CanonicalizeTimeZoneName(isolate, identifier);
}

#else   // V8_INTL_SUPPORT
// #sec-isvalidtimezonename
bool IsValidTimeZoneName(Isolate* isolate, Handle<String> time_zone) {
  return IsUTC(isolate, time_zone);
}
bool IsValidTimeZoneName(Isolate* isolate, const std::string& time_zone) {
  return IsUTC(isolate, time_zone);
}
// #sec-canonicalizetimezonename
Handle<String> CanonicalizeTimeZoneName(Isolate* isolate,
                                        Handle<String> identifier) {
  return isolate->factory()->UTC_string();
}

std::string CanonicalizeTimeZoneName(Isolate* isolate,
                                     const std::string& identifier) {
  std::string utc("UTC");
  return utc;
}
#endif  // V8_INTL_SUPPORT

MaybeHandle<Oddball> IsValidTemporalCalendarField(
    Isolate* isolate, Handle<String> string, Handle<FixedArray> fields_name) {
  Factory* factory = isolate->factory();
  if (!(string->Equals(*(factory->year_string())) ||
        string->Equals(*(factory->month_string())) ||
        string->Equals(*(factory->monthCode_string())) ||
        string->Equals(*(factory->day_string())) ||
        string->Equals(*(factory->hour_string())) ||
        string->Equals(*(factory->minute_string())) ||
        string->Equals(*(factory->second_string())) ||
        string->Equals(*(factory->millisecond_string())) ||
        string->Equals(*(factory->microsecond_string())) ||
        string->Equals(*(factory->nanosecond_string())))) {
    return isolate->factory()->false_value();
  }
  for (int i = 0; i < fields_name->length(); i++) {
    Object item = fields_name->get(i);
    CHECK(item.IsString());
    if (string->Equals(String::cast(item))) {
      return isolate->factory()->false_value();
    }
  }
  return isolate->factory()->true_value();
}
MaybeHandle<Object> ToIntegerThrowOnInfinity(Isolate* isolate,
                                             Handle<Object> argument) {
  TEMPORAL_ENTER_FUNC

  // 1. Let integer be ? ToIntegerOrInfinity(argument).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, argument,
                             Object::ToInteger(isolate, argument), Object);
  // 2. If integer is +∞ or -∞, throw a RangeError exception.
  if (!std::isfinite(argument->Number())) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), Object);
  }
  return argument;
}
// #sec-temporal-balanceisodate
void BalanceISODate(Isolate* isolate, int32_t* year, int32_t* month,
                    int32_t* day) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year, month, and day are integers.
  // 2. Let balancedYearMonth be ! BalanceISOYearMonth(year, month).
  // 3. Set month to balancedYearMonth.[[Month]].
  // 4. Set year to balancedYearMonth.[[Year]].
  BalanceISOYearMonth(isolate, year, month);
  // 5. NOTE: To deal with negative numbers of days whose absolute value is
  // greater than the number of days in a year, the following section subtracts
  // years and adds days until the number of days is greater than −366 or −365.
  // 6. If month > 2, then
  // a. Let testYear be year.
  // 7. Else,
  // a. Let testYear be year − 1.
  int32_t test_year = (*month > 2) ? *year : *year - 1;
  // 8. Repeat, while day < −1 × ! ISODaysInYear(testYear),
  int32_t iso_days_in_year;
  while (*day < -(iso_days_in_year = ISODaysInYear(isolate, test_year))) {
    // a. Set day to day + ! ISODaysInYear(testYear).
    *day += iso_days_in_year;
    // b. Set year to year − 1.
    (*year)--;
    // c. Set testYear to testYear − 1.
    test_year--;
  }
  // 9. NOTE: To deal with numbers of days greater than the number of days in a
  // year, the following section adds years and subtracts days until the number
  // of days is less than 366 or 365.
  // 10. Let testYear be year + 1.
  test_year = (*year) + 1;
  // 11. Repeat, while day > ! ISODaysInYear(testYear),
  while (*day > (iso_days_in_year = ISODaysInYear(isolate, test_year))) {
    // a. Set day to day − ! ISODaysInYear(testYear).
    *day -= iso_days_in_year;
    // b. Set year to year + 1.
    (*year)++;
    // c. Set testYear to testYear + 1.
    test_year++;
  }
  // 12. NOTE: To deal with negative numbers of days whose absolute value is
  // greater than the number of days in the current month, the following section
  // subtracts months and adds days until the number of days is greater than 0.
  // 13. Repeat, while day < 1,
  while (*day < 1) {
    // a. Set balancedYearMonth to ! BalanceISOYearMonth(year, month − 1).
    // b. Set year to balancedYearMonth.[[Year]].
    // c. Set month to balancedYearMonth.[[Month]].
    *month -= 1;
    BalanceISOYearMonth(isolate, year, month);
    // d. Set day to day + ! ISODaysInMonth(year, month).
    *day += ISODaysInMonth(isolate, *year, *month);
  }
  // 14. NOTE: To deal with numbers of days greater than the number of days in
  // the current month, the following section adds months and subtracts days
  // until the number of days is less than the number of days in the month.
  // 15. Repeat, while day > ! ISODaysInMonth(year, month),
  int32_t iso_days_in_month;
  while (*day > (iso_days_in_month = ISODaysInMonth(isolate, *year, *month))) {
    // a. Set day to day − ! ISODaysInMonth(year, month).
    *day -= iso_days_in_month;
    // b. Set balancedYearMonth to ! BalanceISOYearMonth(year, month + 1).
    // c. Set year to balancedYearMonth.[[Year]].
    // d. Set month to balancedYearMonth.[[Month]].
    *month += 1;
    BalanceISOYearMonth(isolate, year, month);
  }
  // 16. Return the new Record { [[Year]]: year, [[Month]]: month, [[Day]]: day
  // }.
  return;
}
// #sec-temporal-isvalidepochnanoseconds
bool IsValidEpochNanoseconds(Isolate* isolate,
                             Handle<BigInt> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(epochNanoseconds) is BigInt.
  // 2. If epochNanoseconds < −86400ℤ × 1017ℤ or epochNanoseconds > 86400ℤ ×
  // 1017ℤ, then a. Return false.
  // 3. Return true.
  Handle<BigInt> upper_bound =
      BigInt::Multiply(
          isolate, BigInt::FromUint64(isolate, 86400),
          BigInt::Exponentiate(isolate, BigInt::FromUint64(isolate, 10),
                               BigInt::FromUint64(isolate, 17))
              .ToHandleChecked())
          .ToHandleChecked();
  Handle<BigInt> lower_bound = BigInt::UnaryMinus(isolate, upper_bound);
  return !(BigInt::CompareToBigInt(epoch_nanoseconds, lower_bound) ==
               ComparisonResult::kLessThan ||
           BigInt::CompareToBigInt(epoch_nanoseconds, upper_bound) ==
               ComparisonResult::kGreaterThan);
}
// #sec-temporal-durationsign
int32_t DurationSign(Isolate* isolaet, const DurationRecord& dur) {
  TEMPORAL_ENTER_FUNC

  // 1. For each value v of « years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds », do a. If v < 0, return
  // −1. b. If v > 0, return 1.
  // 2. Return 0.
  if (dur.years < 0) return -1;
  if (dur.years > 0) return 1;
  if (dur.months < 0) return -1;
  if (dur.months > 0) return 1;
  if (dur.weeks < 0) return -1;
  if (dur.weeks > 0) return 1;
  if (dur.days < 0) return -1;
  if (dur.days > 0) return 1;
  if (dur.hours < 0) return -1;
  if (dur.hours > 0) return 1;
  if (dur.minutes < 0) return -1;
  if (dur.minutes > 0) return 1;
  if (dur.seconds < 0) return -1;
  if (dur.seconds > 0) return 1;
  if (dur.milliseconds < 0) return -1;
  if (dur.milliseconds > 0) return 1;
  if (dur.microseconds < 0) return -1;
  if (dur.microseconds > 0) return 1;
  if (dur.nanoseconds < 0) return -1;
  if (dur.nanoseconds > 0) return 1;
  return 0;
}

// #sec-temporal-isvalidduration
bool IsValidDuration(Isolate* isolate, const DurationRecord& dur) {
  TEMPORAL_ENTER_FUNC

  // 1. Let sign be ! DurationSign(years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds).
  int32_t sign = DurationSign(isolate, dur);
  // 2. For each value v of « years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds », do a. If v is not
  // finite, return false. b. If v < 0 and sign > 0, return false. c. If v > 0
  // and sign < 0, return false.
  // 3. Return true.
  return !((sign > 0 && (dur.years < 0 || dur.months < 0 || dur.weeks < 0 ||
                         dur.days < 0 || dur.hours < 0 || dur.minutes < 0 ||
                         dur.seconds < 0 || dur.milliseconds < 0 ||
                         dur.microseconds < 0 || dur.nanoseconds < 0)) ||
           (sign < 0 && (dur.years > 0 || dur.months > 0 || dur.weeks > 0 ||
                         dur.days > 0 || dur.hours > 0 || dur.minutes > 0 ||
                         dur.seconds > 0 || dur.milliseconds > 0 ||
                         dur.microseconds > 0 || dur.nanoseconds > 0)));
}

// #sec-temporal-isisoleapyear
bool IsISOLeapYear(Isolate* isolate, int32_t year) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year is an integer.
  // 2. If year modulo 4 ≠ 0, return false.
  // 3. If year modulo 400 = 0, return true.
  // 4. If year modulo 100 = 0, return false.
  // 5. Return true.
  return isolate->date_cache()->IsLeap(year);
}

// #sec-temporal-isodaysinmonth
int32_t ISODaysInMonth(Isolate* isolate, int32_t year, int32_t month) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year is an integer.
  // 2. Assert: month is an integer, month ≥ 1, and month ≤ 12.
  DCHECK_GE(month, 1);
  DCHECK_LE(month, 12);
  // 3. If month is 1, 3, 5, 7, 8, 10, or 12, return 31.
  if (month % 2 == ((month < 8) ? 1 : 0)) return 31;
  // 4. If month is 4, 6, 9, or 11, return 30.
  DCHECK(month == 2 || month == 4 || month == 6 || month == 9 || month == 11);
  if (month != 2) return 30;
  // 5. If ! IsISOLeapYear(year) is true, return 29.
  return IsISOLeapYear(isolate, year) ? 29 : 28;
  // 6. Return 28.
}

// #sec-temporal-isodaysinyear
int32_t ISODaysInYear(Isolate* isolate, int32_t year) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year is an integer.
  // 2. If ! IsISOLeapYear(year) is true, then
  // a. Return 366.
  // 3. Return 365.
  return IsISOLeapYear(isolate, year) ? 366 : 365;
}

bool IsValidTime(Isolate* isolate, int32_t hour, int32_t minute, int32_t second,
                 int32_t millisecond, int32_t microsecond, int32_t nanosecond) {
  TEMPORAL_ENTER_FUNC

  return
      // 2. If hour < 0 or hour > 23, then
      // a. Return false.
      hour >= 0 && hour <= 23 &&
      // 3. If minute < 0 or minute > 59, then
      // a. Return false.
      minute >= 0 && minute <= 59 &&
      // 4. If second < 0 or second > 59, then
      // a. Return false.
      second >= 0 && second <= 59 &&
      // 5. If millisecond < 0 or millisecond > 999, then
      // a. Return false.
      millisecond >= 0 && millisecond <= 999 &&
      // 6. If microsecond < 0 or microsecond > 999, then
      // a. Return false.
      microsecond >= 0 && microsecond <= 999 &&
      // 7. If nanosecond < 0 or nanosecond > 999, then
      // a. Return false.
      nanosecond >= 0 && nanosecond <= 999;
  // 8. Return true.
}

// #sec-temporal-isvalidisodate
bool IsValidISODate(Isolate* isolate, int32_t year, int32_t month,
                    int32_t day) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year, month, and day are integers.
  // 2. If month < 1 or month > 12, then
  if (month < 1 || month > 12) return false;
  // a. Return false.
  // 3. Let daysInMonth be ! ISODaysInMonth(year, month).
  // 4. If day < 1 or day > daysInMonth, then
  if (day < 1 || day > ISODaysInMonth(isolate, year, month)) return false;
  // 5. Return true.
  return true;
}
// #sec-temporal-balanceisoyearmonth
void BalanceISOYearMonth(Isolate* isolate, int32_t* year, int32_t* month) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year and month are integers.
  // 2. Set year to year + floor((month - 1) / 12).
  int32_t sign = ((*month - 1) < 0) ? -1 : 1;
  *year += (*month - 1 - ((sign == -1) ? 11 : 0)) / 12;
  // 3. Set month to (month − 1) modulo 12 + 1.
  //
  if (sign > 0) {
    *month = ((*month - 1) % 12) + 1;
  } else {
    *month = (((*month - 1) + 12) % 12) + 1;
  }
  // 4. Return the new Record { [[Year]]: year, [[Month]]: month }.
}
// #sec-temporal-balancetime
DateTimeRecordCommon BalanceTime(Isolate* isolate, int64_t hour, int64_t minute,
                                 int64_t second, int64_t millisecond,
                                 int64_t microsecond, int64_t nanosecond) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: hour, minute, second, millisecond, microsecond, and nanosecond
  // are integers.
  // 2. Set microsecond to microsecond + floor(nanosecond / 1000).
#define FLOOR_DIV(a, b) \
  (((a) / (b)) + ((((a) < 0) && (((a) % (b)) != 0)) ? -1 : 0))
#define MODULO(a, b) ((((a) % (b)) + (b)) % (b))
  microsecond += FLOOR_DIV(nanosecond, 1000L);
  // 3. Set nanosecond to nanosecond modulo 1000.
  nanosecond = MODULO(nanosecond, 1000L);
  // 4. Set millisecond to millisecond + floor(microsecond / 1000).
  millisecond += FLOOR_DIV(microsecond, 1000L);
  // 5. Set microsecond to microsecond modulo 1000.
  microsecond = MODULO(microsecond, 1000L);
  // 6. Set second to second + floor(millisecond / 1000).
  second += FLOOR_DIV(millisecond, 1000L);
  // 7. Set millisecond to millisecond modulo 1000.
  millisecond = MODULO(millisecond, 1000L);
  // 8. Set minute to minute + floor(second / 60).
  minute += FLOOR_DIV(second, 60L);
  // 9. Set second to second modulo 60.
  second = MODULO(second, 60L);
  // 10. Set hour to hour + floor(minute / 60).
  hour += FLOOR_DIV(minute, 60L);
  // 11. Set minute to minute modulo 60.
  minute = MODULO(minute, 60L);
  // 12. Let days be floor(hour / 24).
  int64_t days = FLOOR_DIV(hour, 24L);
  // 13. Set hour to hour modulo 24.
  hour = MODULO(hour, 24L);
#undef FLOOR_DIV
#undef MODULO
  // 14. Return the new Record { [[Days]]: days, [[Hour]]: hour, [[Minute]]:
  // minute, [[Second]]: second, [[Millisecond]]: millisecond, [[Microsecond]]:
  // microsecond, [[Nanosecond]]: nanosecond }.
  return {0,
          0,
          static_cast<int32_t>(days),
          static_cast<int32_t>(hour),
          static_cast<int32_t>(minute),
          static_cast<int32_t>(second),
          static_cast<int32_t>(millisecond),
          static_cast<int32_t>(microsecond),
          static_cast<int32_t>(nanosecond)};
}

}  // namespace temporal

// #sec-temporal.duration
MaybeHandle<JSTemporalDuration> JSTemporalDuration::Constructor(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<Object> years, Handle<Object> months, Handle<Object> weeks,
    Handle<Object> days, Handle<Object> hours, Handle<Object> minutes,
    Handle<Object> seconds, Handle<Object> milliseconds,
    Handle<Object> microseconds, Handle<Object> nanoseconds) {
  const char* method = "Temporal.Duration";
  // 1. If NewTarget is undefined, then
  if (new_target->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                     isolate->factory()->NewStringFromAsciiChecked(method)),
        JSTemporalDuration);
  }
  // 2. Let y be ? ToIntegerThrowOnInfinity(years).
  Handle<Object> number_years;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, number_years,
                             temporal::ToIntegerThrowOnInfinity(isolate, years),
                             JSTemporalDuration);
  int64_t y = NumberToInt64(*number_years);

  // 3. Let mo be ? ToIntegerThrowOnInfinity(months).
  Handle<Object> number_months;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, number_months,
      temporal::ToIntegerThrowOnInfinity(isolate, months), JSTemporalDuration);
  int64_t mo = NumberToInt64(*number_months);

  // 4. Let w be ? ToIntegerThrowOnInfinity(weeks).
  Handle<Object> number_weeks;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, number_weeks,
                             temporal::ToIntegerThrowOnInfinity(isolate, weeks),
                             JSTemporalDuration);
  int64_t w = NumberToInt64(*number_weeks);

  // 5. Let d be ? ToIntegerThrowOnInfinity(days).
  Handle<Object> number_days;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, number_days,
                             temporal::ToIntegerThrowOnInfinity(isolate, days),
                             JSTemporalDuration);
  int64_t d = NumberToInt64(*number_days);

  // 6. Let h be ? ToIntegerThrowOnInfinity(hours).
  Handle<Object> number_hours;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, number_hours,
                             temporal::ToIntegerThrowOnInfinity(isolate, hours),
                             JSTemporalDuration);
  int64_t h = NumberToInt64(*number_hours);

  // 7. Let m be ? ToIntegerThrowOnInfinity(minutes).
  Handle<Object> number_minutes;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, number_minutes,
      temporal::ToIntegerThrowOnInfinity(isolate, minutes), JSTemporalDuration);
  int64_t m = NumberToInt64(*number_minutes);

  // 8. Let s be ? ToIntegerThrowOnInfinity(seconds).
  Handle<Object> number_seconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, number_seconds,
      temporal::ToIntegerThrowOnInfinity(isolate, seconds), JSTemporalDuration);
  int64_t s = NumberToInt64(*number_seconds);

  // 9. Let ms be ? ToIntegerThrowOnInfinity(milliseconds).
  Handle<Object> number_milliseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, number_milliseconds,
      temporal::ToIntegerThrowOnInfinity(isolate, milliseconds),
      JSTemporalDuration);
  int64_t ms = NumberToInt64(*number_milliseconds);

  // 10. Let mis be ? ToIntegerThrowOnInfinity(microseconds).
  Handle<Object> number_microseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, number_microseconds,
      temporal::ToIntegerThrowOnInfinity(isolate, microseconds),
      JSTemporalDuration);
  int64_t mis = NumberToInt64(*number_microseconds);

  // 11. Let ns be ? ToIntegerThrowOnInfinity(nanoseconds).
  Handle<Object> number_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, number_nanoseconds,
      temporal::ToIntegerThrowOnInfinity(isolate, nanoseconds),
      JSTemporalDuration);
  int64_t ns = NumberToInt64(*number_nanoseconds);

  if (!(std::isfinite(number_years->Number()) &&
        std::isfinite(number_months->Number()) &&
        std::isfinite(number_weeks->Number()) &&
        std::isfinite(number_days->Number()) &&
        std::isfinite(number_hours->Number()) &&
        std::isfinite(number_minutes->Number()) &&
        std::isfinite(number_seconds->Number()) &&
        std::isfinite(number_milliseconds->Number()) &&
        std::isfinite(number_microseconds->Number()) &&
        std::isfinite(number_nanoseconds->Number()))) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalDuration);
  }
  // 12. Return ? CreateTemporalDuration(y, mo, w, d, h, m, s, ms, mis, ns,
  // NewTarget).
  return temporal::CreateTemporalDuration(isolate, target, new_target, y, mo, w,
                                          d, h, m, s, ms, mis, ns);
}

// #sec-get-temporal.duration.prototype.sign
MaybeHandle<Smi> JSTemporalDuration::Sign(Isolate* isolate,
                                          Handle<JSTemporalDuration> duration) {
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Return ! DurationSign(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]]).
  return Handle<Smi>(
      Smi::FromInt(temporal::DurationSign(
          isolate,
          {duration->years().Number(), duration->months().Number(),
           duration->weeks().Number(), duration->days().Number(),
           duration->hours().Number(), duration->minutes().Number(),
           duration->seconds().Number(), duration->milliseconds().Number(),
           duration->microseconds().Number(),
           duration->nanoseconds().Number()})),
      isolate);
}

// #sec-get-temporal.duration.prototype.blank
MaybeHandle<Oddball> JSTemporalDuration::Blank(
    Isolate* isolate, Handle<JSTemporalDuration> duration) {
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Let sign be ! DurationSign(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]]).
  // 4. If sign = 0, return true.
  // 5. Return false.
  int32_t sign = temporal::DurationSign(
      isolate,
      {duration->years().Number(), duration->months().Number(),
       duration->weeks().Number(), duration->days().Number(),
       duration->hours().Number(), duration->minutes().Number(),
       duration->seconds().Number(), duration->milliseconds().Number(),
       duration->microseconds().Number(), duration->nanoseconds().Number()});
  return sign == 0 ? isolate->factory()->true_value()
                   : isolate->factory()->false_value();
}

// #sec-temporal.calendar
MaybeHandle<JSTemporalCalendar> JSTemporalCalendar::Constructor(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<Object> identifier_obj) {
  // 1. If NewTarget is undefined, then
  if (new_target->IsUndefined(isolate)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kConstructorNotFunction,
                                 isolate->factory()->NewStringFromStaticChars(
                                     "Temporal.Calendar")),
                    JSTemporalCalendar);
  }
  // 2. Set identifier to ? ToString(identifier).
  Handle<String> identifier;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, identifier,
                             Object::ToString(isolate, identifier_obj),
                             JSTemporalCalendar);
  // 3. If ! IsBuiltinCalendar(id) is false, then
  if (!temporal::IsBuiltinCalendar(isolate, identifier)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(
        isolate, NewRangeError(MessageTemplate::kInvalidCalendar, identifier),
        JSTemporalCalendar);
  }
  return temporal::CreateTemporalCalendar(isolate, target, new_target,
                                          identifier);
}

// #sec-temporal.calendar.prototype.tostring
Handle<String> JSTemporalCalendar::ToString(Isolate* isolate,
                                            Handle<JSTemporalCalendar> calendar,
                                            const char* method) {
  return isolate->factory()->iso8601_string();
}

// #sec-temporal.timezone
MaybeHandle<JSTemporalTimeZone> JSTemporalTimeZone::Constructor(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<Object> identifier_obj) {
  // 1. If NewTarget is undefined, then
  if (new_target->IsUndefined(isolate)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kConstructorNotFunction,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "Temporal.TimeZone")),
                    JSTemporalTimeZone);
  }
  // 2. Set identifier to ? ToString(identifier).
  Handle<String> identifier;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, identifier,
                             Object::ToString(isolate, identifier_obj),
                             JSTemporalTimeZone);
  Handle<String> canonical;
  // 3. If identifier satisfies the syntax of a TimeZoneNumericUTCOffset
  // (see 13.33), then
  if (temporal::IsValidTimeZoneNumericUTCOffsetString(isolate, identifier)) {
    // a. Let offsetNanoseconds be ? ParseTimeZoneOffsetString(identifier).
    Maybe<int64_t> maybe_offset_nanoseconds =
        temporal::ParseTimeZoneOffsetString(isolate, identifier);
    MAYBE_RETURN(maybe_offset_nanoseconds, Handle<JSTemporalTimeZone>());
    int64_t offset_nanoseconds = maybe_offset_nanoseconds.FromJust();

    // b. Let canonical be ! FormatTimeZoneOffsetString(offsetNanoseconds).
    canonical = isolate->factory()->NewStringFromAsciiChecked(
        temporal::FormatTimeZoneOffsetString(offset_nanoseconds).c_str());
  } else {
    // 4. Else,
    // a. If ! IsValidTimeZoneName(identifier) is false, then
    if (!temporal::IsValidTimeZoneName(isolate, identifier)) {
      // i. Throw a RangeError exception.
      THROW_NEW_ERROR(
          isolate, NewRangeError(MessageTemplate::kInvalidTimeZone, identifier),
          JSTemporalTimeZone);
    }
    // b. Let canonical be ! CanonicalizeTimeZoneName(identifier).
    canonical = temporal::CanonicalizeTimeZoneName(isolate, identifier);
  }
  // 5. Return ? CreateTemporalTimeZone(canonical, NewTarget).
  return temporal::CreateTemporalTimeZone(isolate, target, new_target,
                                          canonical);
}

// #sec-temporal.timezone.prototype.tostring
Handle<Object> JSTemporalTimeZone::ToString(
    Isolate* isolate, Handle<JSTemporalTimeZone> time_zone,
    const char* method) {
  return isolate->factory()->NewStringFromAsciiChecked(time_zone->id().c_str());
}

int64_t JSTemporalTimeZone::offset_nanoseconds() const {
  CHECK(is_offset());
  return 1000000L * offset_milliseconds();
}

std::string JSTemporalTimeZone::id() const {
  if (is_offset()) {
    return temporal::FormatTimeZoneOffsetString(offset_nanoseconds());
  }
#ifdef V8_INTL_SUPPORT
  return Intl::TimeZoneIdFromIndex(offset_milliseconds_or_time_zone_index());
#else   // V8_INTL_SUPPORT
  CHECK_EQ(0, offset_milliseconds_or_time_zone_index());
  return "UTC";
#endif  // V8_INTL_SUPPORT
}

MaybeHandle<JSTemporalPlainDate> JSTemporalPlainDate::Constructor(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<Object> iso_year_obj, Handle<Object> iso_month_obj,
    Handle<Object> iso_day_obj, Handle<Object> calendar_like) {
  const char* method = "Temporal.PlainDate";
  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (new_target->IsUndefined()) {
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                     isolate->factory()->NewStringFromAsciiChecked(method)),
        JSTemporalPlainDate);
  }
#define CHECK_FIELD(name, T)                                       \
  Handle<Object> number_##name;                                    \
  /* x. Let name be ? ToIntegerThrowOnInfinity(name). */           \
  ASSIGN_RETURN_ON_EXCEPTION(                                      \
      isolate, number_##name,                                      \
      temporal::ToIntegerThrowOnInfinity(isolate, name##_obj), T); \
  int32_t name = NumberToInt32(*number_##name);

  CHECK_FIELD(iso_year, JSTemporalPlainDate);
  CHECK_FIELD(iso_month, JSTemporalPlainDate);
  CHECK_FIELD(iso_day, JSTemporalPlainDate);

  // 8. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             temporal::ToTemporalCalendarWithISODefault(
                                 isolate, calendar_like, method),
                             JSTemporalPlainDate);

  // 9. Return ? CreateTemporalDate(y, m, d, calendar, NewTarget).
  return temporal::CreateTemporalDate(isolate, target, new_target, iso_year,
                                      iso_month, iso_day, calendar);
}

#define ADD_INT_FIELD(obj, str, field, item)                   \
  CHECK(JSReceiver::CreateDataProperty(                        \
            isolate, obj, factory->str##_string(),             \
            Handle<Smi>(Smi::FromInt(item->field()), isolate), \
            Just(kThrowOnError))                               \
            .FromJust());

// #sec-temporal.plaindate.prototype.getisofields
MaybeHandle<JSReceiver> JSTemporalPlainDate::GetISOFields(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date) {
  Factory* factory = isolate->factory();
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Let fields be ! OrdinaryObjectCreate(%Object.prototype%).
  Handle<JSObject> fields =
      isolate->factory()->NewJSObject(isolate->object_function());
  // 4. Perform ! CreateDataPropertyOrThrow(fields, "calendar",
  // temporalDate.[[Calendar]]).
  CHECK(JSReceiver::CreateDataProperty(
            isolate, fields, factory->calendar_string(),
            Handle<JSReceiver>(temporal_date->calendar(), isolate),
            Just(kThrowOnError))
            .FromJust());
  // 5. Perform ! CreateDataPropertyOrThrow(fields, "isoDay",
  // 𝔽(temporalDate.[[ISODay]])).
  // 6. Perform ! CreateDataPropertyOrThrow(fields, "isoMonth",
  // 𝔽(temporalDate.[[ISOMonth]])).
  // 7. Perform ! CreateDataPropertyOrThrow(fields, "isoYear",
  // 𝔽(temporalDate.[[ISOYear]])).
  ADD_INT_FIELD(fields, isoDay, iso_day, temporal_date)
  ADD_INT_FIELD(fields, isoMonth, iso_month, temporal_date)
  ADD_INT_FIELD(fields, isoYear, iso_year, temporal_date)
  // 8. Return fields.
  return fields;
}

// #sec-temporal-createtemporaldatetime
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Constructor(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<Object> iso_year_obj, Handle<Object> iso_month_obj,
    Handle<Object> iso_day_obj, Handle<Object> hour_obj,
    Handle<Object> minute_obj, Handle<Object> second_obj,
    Handle<Object> millisecond_obj, Handle<Object> microsecond_obj,
    Handle<Object> nanosecond_obj, Handle<Object> calendar_like) {
  const char* method = "Temporal.PlainDateTime";
  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (new_target->IsUndefined()) {
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                     isolate->factory()->NewStringFromAsciiChecked(method)),
        JSTemporalPlainDateTime);
  }

  CHECK_FIELD(iso_year, JSTemporalPlainDateTime);
  CHECK_FIELD(iso_month, JSTemporalPlainDateTime);
  CHECK_FIELD(iso_day, JSTemporalPlainDateTime);
  CHECK_FIELD(hour, JSTemporalPlainDateTime);
  CHECK_FIELD(minute, JSTemporalPlainDateTime);
  CHECK_FIELD(second, JSTemporalPlainDateTime);
  CHECK_FIELD(millisecond, JSTemporalPlainDateTime);
  CHECK_FIELD(microsecond, JSTemporalPlainDateTime);
  CHECK_FIELD(nanosecond, JSTemporalPlainDateTime);

  // 20. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             temporal::ToTemporalCalendarWithISODefault(
                                 isolate, calendar_like, method),
                             JSTemporalPlainDateTime);

  // 21. Return ? CreateTemporalDateTime(isoYear, isoMonth, isoDay, hour,
  // minute, second, millisecond, microsecond, nanosecond, calendar, NewTarget).
  return temporal::CreateTemporalDateTime(
      isolate, target, new_target, iso_year, iso_month, iso_day, hour, minute,
      second, millisecond, microsecond, nanosecond, calendar);
}

// #sec-temporal.plaindatetime.prototype.getisofields
MaybeHandle<JSReceiver> JSTemporalPlainDateTime::GetISOFields(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time) {
  Factory* factory = isolate->factory();
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Let fields be ! OrdinaryObjectCreate(%Object.prototype%).
  Handle<JSObject> fields =
      isolate->factory()->NewJSObject(isolate->object_function());
  // 4. Perform ! CreateDataPropertyOrThrow(fields, "calendar",
  // temporalTime.[[Calendar]]).
  CHECK(JSReceiver::CreateDataProperty(
            isolate, fields, factory->calendar_string(),
            Handle<JSReceiver>(date_time->calendar(), isolate),
            Just(kThrowOnError))
            .FromJust());
  // 5. Perform ! CreateDataPropertyOrThrow(fields, "isoDay",
  // 𝔽(dateTime.[[ISODay]])).
  // 6. Perform ! CreateDataPropertyOrThrow(fields, "isoHour",
  // 𝔽(temporalTime.[[ISOHour]])).
  // 7. Perform ! CreateDataPropertyOrThrow(fields, "isoMicrosecond",
  // 𝔽(temporalTime.[[ISOMicrosecond]])).
  // 8. Perform ! CreateDataPropertyOrThrow(fields, "isoMillisecond",
  // 𝔽(temporalTime.[[ISOMillisecond]])).
  // 9. Perform ! CreateDataPropertyOrThrow(fields, "isoMinute",
  // 𝔽(temporalTime.[[ISOMinute]])).
  // 10. Perform ! CreateDataPropertyOrThrow(fields, "isoMonth",
  // 𝔽(temporalTime.[[ISOMonth]])).
  // 11. Perform ! CreateDataPropertyOrThrow(fields, "isoNanosecond",
  // 𝔽(temporalTime.[[ISONanosecond]])).
  // 12. Perform ! CreateDataPropertyOrThrow(fields, "isoSecond",
  // 𝔽(temporalTime.[[ISOSecond]])).
  // 13. Perform ! CreateDataPropertyOrThrow(fields, "isoYear",
  // 𝔽(temporalTime.[[ISOYear]])).
  ADD_INT_FIELD(fields, isoDay, iso_day, date_time)
  ADD_INT_FIELD(fields, isoHour, iso_hour, date_time)
  ADD_INT_FIELD(fields, isoMicrosecond, iso_microsecond, date_time)
  ADD_INT_FIELD(fields, isoMillisecond, iso_millisecond, date_time)
  ADD_INT_FIELD(fields, isoMinute, iso_minute, date_time)
  ADD_INT_FIELD(fields, isoMonth, iso_month, date_time)
  ADD_INT_FIELD(fields, isoNanosecond, iso_nanosecond, date_time)
  ADD_INT_FIELD(fields, isoSecond, iso_second, date_time)
  ADD_INT_FIELD(fields, isoYear, iso_year, date_time)
  // 14. Return fields.
  return fields;
}

// #sec-temporal.plainmonthday
MaybeHandle<JSTemporalPlainMonthDay> JSTemporalPlainMonthDay::Constructor(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<Object> iso_month_obj, Handle<Object> iso_day_obj,
    Handle<Object> calendar_like, Handle<Object> reference_iso_year_obj) {
  const char* method = "Temporal.PlainMonthDay";
  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (new_target->IsUndefined()) {
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                     isolate->factory()->NewStringFromAsciiChecked(method)),
        JSTemporalPlainMonthDay);
  }

  // 3. Let m be ? ToIntegerThrowOnInfinity(isoMonth).
  CHECK_FIELD(iso_month, JSTemporalPlainMonthDay);
  // 5. Let d be ? ToIntegerThrowOnInfinity(isoDay).
  CHECK_FIELD(iso_day, JSTemporalPlainMonthDay);
  // 7. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             temporal::ToTemporalCalendarWithISODefault(
                                 isolate, calendar_like, method),
                             JSTemporalPlainMonthDay);

  // 2. If referenceISOYear is undefined, then
  // a. Set referenceISOYear to 1972𝔽.
  // ...
  // 8. Let ref be ? ToIntegerThrowOnInfinity(referenceISOYear).
  int32_t ref = 1972;
  if (!reference_iso_year_obj->IsUndefined()) {
    CHECK_FIELD(reference_iso_year, JSTemporalPlainMonthDay);
    ref = reference_iso_year;
  }

  // 10. Return ? CreateTemporalMonthDay(y, m, calendar, ref, NewTarget).
  return temporal::CreateTemporalMonthDay(isolate, target, new_target,
                                          iso_month, iso_day, calendar, ref);
}

// #sec-temporal.plainmonthday.prototype.getisofields
MaybeHandle<JSReceiver> JSTemporalPlainMonthDay::GetISOFields(
    Isolate* isolate, Handle<JSTemporalPlainMonthDay> month_day) {
  Factory* factory = isolate->factory();
  // 1. Let monthDay be the this value.
  // 2. Perform ? RequireInternalSlot(monthDay,
  // [[InitializedTemporalMonthDay]]).
  // 3. Let fields be ! OrdinaryObjectCreate(%Object.prototype%).
  Handle<JSObject> fields = factory->NewJSObject(isolate->object_function());
  // 4. Perform ! CreateDataPropertyOrThrow(fields, "calendar",
  // montyDay.[[Calendar]]).
  CHECK(JSReceiver::CreateDataProperty(
            isolate, fields, factory->calendar_string(),
            Handle<JSReceiver>(month_day->calendar(), isolate),
            Just(kThrowOnError))
            .FromJust());

  // 5. Perform ! CreateDataPropertyOrThrow(fields, "isoDay",
  // 𝔽(montyDay.[[ISODay]])).
  // 6. Perform ! CreateDataPropertyOrThrow(fields, "isoMonth",
  // 𝔽(montyDay.[[ISOMonth]])).
  // 7. Perform ! CreateDataPropertyOrThrow(fields, "isoYear",
  // 𝔽(montyDay.[[ISOYear]])).
  ADD_INT_FIELD(fields, isoDay, iso_day, month_day)
  ADD_INT_FIELD(fields, isoMonth, iso_month, month_day)
  ADD_INT_FIELD(fields, isoYear, iso_year, month_day)
  // 8. Return fields.
  return fields;
}

MaybeHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::Constructor(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<Object> iso_year_obj, Handle<Object> iso_month_obj,
    Handle<Object> calendar_like, Handle<Object> reference_iso_day_obj) {
  const char* method = "Temporal.PlainYearMonth";
  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (new_target->IsUndefined()) {
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                     isolate->factory()->NewStringFromAsciiChecked(method)),
        JSTemporalPlainYearMonth);
  }
  // 7. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  // 10. Return ? CreateTemporalYearMonth(y, m, calendar, ref, NewTarget).

  // 3. Let y be ? ToIntegerThrowOnInfinity(isoYear).
  CHECK_FIELD(iso_year, JSTemporalPlainYearMonth);
  // 5. Let m be ? ToIntegerThrowOnInfinity(isoMonth).
  CHECK_FIELD(iso_month, JSTemporalPlainYearMonth);
  // 7. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             temporal::ToTemporalCalendarWithISODefault(
                                 isolate, calendar_like, method),
                             JSTemporalPlainYearMonth);

  // 2. If referenceISODay is undefined, then
  // a. Set referenceISODay to 1𝔽.
  // ...
  // 8. Let ref be ? ToIntegerThrowOnInfinity(referenceISODay).
  int32_t ref = 1;
  if (!reference_iso_day_obj->IsUndefined()) {
    CHECK_FIELD(reference_iso_day, JSTemporalPlainYearMonth);
    ref = reference_iso_day;
  }

  // 10. Return ? CreateTemporalYearMonth(y, m, calendar, ref, NewTarget).
  return temporal::CreateTemporalYearMonth(isolate, target, new_target,
                                           iso_year, iso_month, calendar, ref);
}

// #sec-temporal.plainyearmonth.prototype.getisofields
MaybeHandle<JSReceiver> JSTemporalPlainYearMonth::GetISOFields(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month) {
  Factory* factory = isolate->factory();
  // 1. Let yearMonth be the this value.
  // 2. Perform ? RequireInternalSlot(yearMonth,
  // [[InitializedTemporalYearMonth]]).
  // 3. Let fields be ! OrdinaryObjectCreate(%Object.prototype%).
  Handle<JSObject> fields =
      isolate->factory()->NewJSObject(isolate->object_function());
  // 4. Perform ! CreateDataPropertyOrThrow(fields, "calendar",
  // yearMonth.[[Calendar]]).
  CHECK(JSReceiver::CreateDataProperty(
            isolate, fields, factory->calendar_string(),
            Handle<JSReceiver>(year_month->calendar(), isolate),
            Just(kThrowOnError))
            .FromJust());
  // 5. Perform ! CreateDataPropertyOrThrow(fields, "isoDay",
  // 𝔽(yearMonth.[[ISODay]])).
  // 6. Perform ! CreateDataPropertyOrThrow(fields, "isoMonth",
  // 𝔽(yearMonth.[[ISOMonth]])).
  // 7. Perform ! CreateDataPropertyOrThrow(fields, "isoYear",
  // 𝔽(yearMonth.[[ISOYear]])).
  ADD_INT_FIELD(fields, isoDay, iso_day, year_month)
  ADD_INT_FIELD(fields, isoMonth, iso_month, year_month)
  ADD_INT_FIELD(fields, isoYear, iso_year, year_month)
  // 8. Return fields.
  return fields;
}

// #sec-temporal-plaintime-constructor
MaybeHandle<JSTemporalPlainTime> JSTemporalPlainTime::Constructor(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<Object> hour_obj, Handle<Object> minute_obj,
    Handle<Object> second_obj, Handle<Object> millisecond_obj,
    Handle<Object> microsecond_obj, Handle<Object> nanosecond_obj) {
  const char* method = "Temporal.PlainTime";
  // 1. If NewTarget is undefined, then
  // a. Throw a TypeError exception.
  if (new_target->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                     isolate->factory()->NewStringFromAsciiChecked(method)),
        JSTemporalPlainTime);
  }

  CHECK_FIELD(hour, JSTemporalPlainTime);
  CHECK_FIELD(minute, JSTemporalPlainTime);
  CHECK_FIELD(second, JSTemporalPlainTime);
  CHECK_FIELD(millisecond, JSTemporalPlainTime);
  CHECK_FIELD(microsecond, JSTemporalPlainTime);
  CHECK_FIELD(nanosecond, JSTemporalPlainTime);

  // 14. Return ? CreateTemporalTime(hour, minute, second, millisecond,
  // microsecond, nanosecond, NewTarget).
  return temporal::CreateTemporalTime(isolate, target, new_target, hour, minute,
                                      second, millisecond, microsecond,
                                      nanosecond);
}

// #sec-temporal.plaintime.prototype.getisofields
MaybeHandle<JSReceiver> JSTemporalPlainTime::GetISOFields(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time) {
  Factory* factory = isolate->factory();
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. Let fields be ! OrdinaryObjectCreate(%Object.prototype%).
  Handle<JSObject> fields =
      isolate->factory()->NewJSObject(isolate->object_function());
  // 4. Perform ! CreateDataPropertyOrThrow(fields, "calendar",
  // temporalTime.[[Calendar]]).
  Handle<JSTemporalCalendar> iso8601_calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, iso8601_calendar,
                             temporal::GetISO8601Calendar(isolate),
                             JSTemporalPlainTime);
  CHECK(JSReceiver::CreateDataProperty(isolate, fields,
                                       factory->calendar_string(),
                                       iso8601_calendar, Just(kThrowOnError))
            .FromJust());

  // 5. Perform ! CreateDataPropertyOrThrow(fields, "isoHour",
  // 𝔽(temporalTime.[[ISOHour]])).
  // 6. Perform ! CreateDataPropertyOrThrow(fields, "isoMicrosecond",
  // 𝔽(temporalTime.[[ISOMicrosecond]])).
  // 7. Perform ! CreateDataPropertyOrThrow(fields, "isoMillisecond",
  // 𝔽(temporalTime.[[ISOMillisecond]])).
  // 8. Perform ! CreateDataPropertyOrThrow(fields, "isoMinute",
  // 𝔽(temporalTime.[[ISOMinute]])).
  // 9. Perform ! CreateDataPropertyOrThrow(fields, "isoNanosecond",
  // 𝔽(temporalTime.[[ISONanosecond]])).
  // 10. Perform ! CreateDataPropertyOrThrow(fields, "isoSecond",
  // 𝔽(temporalTime.[[ISOSecond]])).
  ADD_INT_FIELD(fields, isoHour, iso_hour, temporal_time)
  ADD_INT_FIELD(fields, isoMicrosecond, iso_microsecond, temporal_time)
  ADD_INT_FIELD(fields, isoMillisecond, iso_millisecond, temporal_time)
  ADD_INT_FIELD(fields, isoMinute, iso_minute, temporal_time)
  ADD_INT_FIELD(fields, isoNanosecond, iso_nanosecond, temporal_time)
  ADD_INT_FIELD(fields, isoSecond, iso_second, temporal_time)
  // 11. Return fields.
  return fields;
}

// #sec-temporal.zoneddatetime
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Constructor(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<Object> epoch_nanoseconds_obj, Handle<Object> time_zone_like,
    Handle<Object> calendar_like) {
  const char* method = "Temporal.ZonedDateTime";
  // 1. If NewTarget is undefined, then
  if (new_target->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                     isolate->factory()->NewStringFromAsciiChecked(method)),
        JSTemporalZonedDateTime);
  }
  // 2. Set epochNanoseconds to ? ToBigInt(epochNanoseconds).
  Handle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, epoch_nanoseconds,
                             BigInt::FromObject(isolate, epoch_nanoseconds_obj),
                             JSTemporalZonedDateTime);
  // 3. If ! IsValidEpochNanoseconds(epochNanoseconds) is false, throw a
  // RangeError exception.
  if (!temporal::IsValidEpochNanoseconds(isolate, epoch_nanoseconds)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalZonedDateTime);
  }

  // 4. Let timeZone be ? ToTemporalTimeZone(timeZoneLike).
  Handle<JSReceiver> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      temporal::ToTemporalTimeZone(isolate, time_zone_like, method),
      JSTemporalZonedDateTime);

  // 5. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             temporal::ToTemporalCalendarWithISODefault(
                                 isolate, calendar_like, method),
                             JSTemporalZonedDateTime);

  // 6. Return ? CreateTemporalZonedDateTime(epochNanoseconds, timeZone,
  // calendar, NewTarget).
  return temporal::CreateTemporalZonedDateTime(
      isolate, target, new_target, epoch_nanoseconds, time_zone, calendar);
}

// #sec-temporal.zoneddatetime.prototype.getisofields
MaybeHandle<JSReceiver> JSTemporalZonedDateTime::GetISOFields(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  Factory* factory = isolate->factory();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let fields be ! OrdinaryObjectCreate(%Object.prototype%).
  Handle<JSObject> fields =
      isolate->factory()->NewJSObject(isolate->object_function());
  // 4. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 5. Let instant be ? CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      JSReceiver);

  // 6. Let calendar be zonedDateTime.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  // 7. Let dateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant,
  // calendar).
  Handle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, date_time,
                             temporal::BuiltinTimeZoneGetPlainDateTimeFor(
                                 isolate, time_zone, instant, calendar),
                             JSReceiver);
  // 8. Let offset be ? BuiltinTimeZoneGetOffsetStringFor(timeZone, instant).
  Handle<String> offset;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, offset,
      temporal::BuiltinTimeZoneGetOffsetStringFor(isolate, time_zone, instant),
      JSReceiver);

#define ADD_STRING_FIELD(obj, str, field)                                     \
  CHECK(JSReceiver::CreateDataProperty(isolate, obj, factory->str##_string(), \
                                       field, Just(kThrowOnError))            \
            .FromJust());

  // 9. Perform ! CreateDataPropertyOrThrow(fields, "calendar", calendar).
  // 10. Perform ! CreateDataPropertyOrThrow(fields, "isoDay",
  // 𝔽(dateTime.[[ISODay]])).
  // 11. Perform ! CreateDataPropertyOrThrow(fields, "isoHour",
  // 𝔽(temporalTime.[[ISOHour]])).
  // 12. Perform ! CreateDataPropertyOrThrow(fields, "isoMicrosecond",
  // 𝔽(temporalTime.[[ISOMicrosecond]])).
  // 13. Perform ! CreateDataPropertyOrThrow(fields, "isoMillisecond",
  // 𝔽(temporalTime.[[ISOMillisecond]])).
  // 14. Perform ! CreateDataPropertyOrThrow(fields, "isoMinute",
  // 𝔽(temporalTime.[[ISOMinute]])).
  // 15. Perform ! CreateDataPropertyOrThrow(fields, "isoMonth",
  // 𝔽(temporalTime.[[ISOMonth]])).
  // 16. Perform ! CreateDataPropertyOrThrow(fields, "isoNanosecond",
  // 𝔽(temporalTime.[[ISONanosecond]])).
  // 17. Perform ! CreateDataPropertyOrThrow(fields, "isoSecond",
  // 𝔽(temporalTime.[[ISOSecond]])).
  // 18. Perform ! CreateDataPropertyOrThrow(fields, "isoYear",
  // 𝔽(temporalTime.[[ISOYear]])).
  // 19. Perform ! CreateDataPropertyOrThrow(fields, "offset", offset).
  // 20. Perform ! CreateDataPropertyOrThrow(fields, "timeZone", timeZone).
  ADD_STRING_FIELD(fields, calendar, calendar)
  ADD_INT_FIELD(fields, isoDay, iso_day, date_time)
  ADD_INT_FIELD(fields, isoHour, iso_hour, date_time)
  ADD_INT_FIELD(fields, isoMicrosecond, iso_microsecond, date_time)
  ADD_INT_FIELD(fields, isoMillisecond, iso_millisecond, date_time)
  ADD_INT_FIELD(fields, isoMinute, iso_minute, date_time)
  ADD_INT_FIELD(fields, isoMonth, iso_month, date_time)
  ADD_INT_FIELD(fields, isoSecond, iso_second, date_time)
  ADD_INT_FIELD(fields, isoYear, iso_year, date_time)
  ADD_STRING_FIELD(fields, offset, offset)
  ADD_STRING_FIELD(fields, timeZone, time_zone)
  // 21. Return fields.
  return fields;
}

MaybeHandle<JSTemporalInstant> JSTemporalInstant::Constructor(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<Object> epoch_nanoseconds_obj) {
  const char* method = "Temporal.Instant";
  // 1. If NewTarget is undefined, then
  if (new_target->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                     isolate->factory()->NewStringFromAsciiChecked(method)),
        JSTemporalInstant);
  }
  // 2. Let epochNanoseconds be ? ToBigInt(epochNanoseconds).
  Handle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, epoch_nanoseconds,
                             BigInt::FromObject(isolate, epoch_nanoseconds_obj),
                             JSTemporalInstant);
  // 3. If ! IsValidEpochNanoseconds(epochNanoseconds) is false, throw a
  // RangeError exception.
  if (!temporal::IsValidEpochNanoseconds(isolate, epoch_nanoseconds)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalInstant);
  }
  // 4. Return ? CreateTemporalInstant(epochNanoseconds, NewTarget).
  return temporal::CreateTemporalInstant(isolate, target, new_target,
                                         epoch_nanoseconds);
}

}  // namespace internal
}  // namespace v8
