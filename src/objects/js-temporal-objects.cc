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
#ifdef V8_INTL_SUPPORT
#include "unicode/calendar.h"
#include "unicode/unistr.h"
#endif  // V8_INTL_SUPPORT

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

struct InstantRecord : public DateTimeRecordCommon {
  std::string offset_string;
};
struct ZonedDateTimeRecord : public InstantRecord {
  std::string time_zone_name;
  std::string calendar;
  bool time_zone_z;
};

struct DateRecord {
  int32_t year;
  int32_t month;
  int32_t day;
  std::string calendar;
};

struct DateTimeRecord : public DateTimeRecordCommon {
  std::string calendar;
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

struct TimeRecord {
  int32_t hour;
  int32_t minute;
  int32_t second;
  int32_t millisecond;
  int32_t microsecond;
  int32_t nanosecond;
  std::string calendar;
};

struct TimeZoneRecord {
  bool z;
  std::string offset_string;
  std::string name;
};

// Options

enum class Unit {
  kNotPresent,
  kAuto,
  kYear,
  kMonth,
  kWeek,
  kDay,
  kHour,
  kMinute,
  kSecond,
  kMillisecond,
  kMicrosecond,
  kNanosecond
};

V8_WARN_UNUSED_RESULT Handle<String> UnitToString(Isolate* isolate, Unit unit);
// #sec-temporal-defaulttemporallargestunit
V8_WARN_UNUSED_RESULT Unit
DefaultTemporalLargestUnit(Isolate* isolate, const DurationRecord& duration);

// #sec-temporal-largeroftwotemporalunits
V8_WARN_UNUSED_RESULT Unit LargerOfTwoTemporalUnits(Isolate* isolate, Unit u1,
                                                    Unit u2);

// #sec-temporal-tolargesttemporalunit
V8_WARN_UNUSED_RESULT Maybe<Unit> ToLargestTemporalUnit(
    Isolate* isolate, Handle<JSReceiver> normalized_options,
    std::set<Unit> disallowed_units, Unit fallback, Unit auto_value,
    const char* method);
// #sec-temporal-tolargesttemporalunit
V8_WARN_UNUSED_RESULT Maybe<Unit> ToSmallestTemporalUnit(
    Isolate* isolate, Handle<JSReceiver> normalized_options,
    std::set<Unit> disallowed_units, Unit fallback, const char* method);
// #sec-temporal-tolargesttemporalunit
V8_WARN_UNUSED_RESULT Maybe<Unit> ToTemporalDurationTotalUnit(
    Isolate* isolate, Handle<JSReceiver> normalized_options,
    const char* method);
// #sec-temporal-validatetemporalunitrange
V8_WARN_UNUSED_RESULT Maybe<bool> ValidateTemporalUnitRange(Isolate* isolate,
                                                            Unit largest_unit,
                                                            Unit smallest_unit,
                                                            const char* method);

// #sec-temporal-maximumtemporaldurationroundingincrement
V8_WARN_UNUSED_RESULT Maybe<bool> MaximumTemporalDurationRoundingIncrement(
    Isolate* isolate, Unit unit, double* maximum);

enum class Precision { k0, k1, k2, k3, k4, k5, k6, k7, k8, k9, kAuto, kMinute };

// #sec-temporal-toshowcalendaroption
enum class ShowCalendar { kAuto, kAlways, kNever };

V8_WARN_UNUSED_RESULT Maybe<ShowCalendar> ToShowCalendarOption(
    Isolate* isolate, Handle<JSReceiver> options, const char* method);

// #sec-temporal-toshowtimezonenameoption
enum class ShowTimeZone { kAuto, kNever };
V8_WARN_UNUSED_RESULT Maybe<ShowTimeZone> ToShowTimeZoneNameOption(
    Isolate* isolate, Handle<JSReceiver> options, const char* method);

// #sec-temporal-totemporaloverflow
enum class ShowOverflow { kConstrain, kReject };
V8_WARN_UNUSED_RESULT Maybe<ShowOverflow> ToTemporalOverflow(
    Isolate* isolate, Handle<JSReceiver> options, const char* method);

V8_WARN_UNUSED_RESULT Handle<String> ShowOverflowToString(
    Isolate* isolate, ShowOverflow overflow);

// #sec-temporal-totemporaldisambiguation
enum class Disambiguation { kCompatible, kEarlier, kLater, kReject };
V8_WARN_UNUSED_RESULT Maybe<Disambiguation> ToTemporalDisambiguation(
    Isolate* isolate, Handle<JSReceiver> options, const char* method);

// sec-temporal-totemporalroundingmode
enum class RoundingMode { kCeil, kFloor, kTrunc, kHalfExpand };
V8_WARN_UNUSED_RESULT Maybe<RoundingMode> ToTemporalRoundingMode(
    Isolate* isolate, Handle<JSReceiver> options, RoundingMode fallback,
    const char* method);

// #sec-temporal-negatetemporalroundingmode
V8_WARN_UNUSED_RESULT RoundingMode
NegateTemporalRoundingMode(Isolate* isolate, RoundingMode rounding_mode);

// #sec-temporal-toshowoffsetoption
enum class ShowOffset { kAuto, kNever };
V8_WARN_UNUSED_RESULT Maybe<ShowOffset> ToShowOffsetOption(
    Isolate* isolate, Handle<JSReceiver> options, const char* method);

// #sec-temporal-totemporaloffset
enum class Offset { kPrefer, kUse, kIgnore, kReject };
V8_WARN_UNUSED_RESULT Maybe<Offset> ToTemporalOffset(Isolate* isolate,
                                                     Handle<JSReceiver> options,
                                                     Offset fallback,
                                                     const char* method);

V8_WARN_UNUSED_RESULT Maybe<bool> ToSecondsStringPrecision(
    Isolate* isolate, Handle<JSReceiver> options, Precision* precision,
    double* increment, Unit* unit, const char* method);

V8_WARN_UNUSED_RESULT Maybe<int> ToTemporalRoundingIncrement(
    Isolate* isolate, Handle<JSReceiver> normalized_options, int dividen,
    bool dividen_is_defined, bool inclusive, const char* method);

V8_WARN_UNUSED_RESULT Maybe<int> ToTemporalDateTimeRoundingIncrement(
    Isolate* isolate, Handle<JSReceiver> normalized_options, Unit smallest_unit,
    const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<Object> ToIntegerThrowOnInfinity(
    Isolate* isoalte, Handle<Object> num);

V8_WARN_UNUSED_RESULT MaybeHandle<Object> ToPositiveInteger(Isolate* isoalte,
                                                            Handle<Object> num);

V8_WARN_UNUSED_RESULT Maybe<bool> RejectTemporalCalendarType(
    Isolate* isolate, Handle<Object> object);

// Field Operations

// #sec-temporal-mergelargestunitoption
V8_WARN_UNUSED_RESULT MaybeHandle<JSObject> MergeLargestUnitOption(
    Isolate* isolate, Handle<JSReceiver> options, Unit largest_unit);

// #sec-temporal-preparetemporalfields
V8_WARN_UNUSED_RESULT MaybeHandle<JSObject> PrepareTemporalFields(
    Isolate* isolate, Handle<JSReceiver> fields, Handle<FixedArray> field_names,
    bool require_day, bool require_time_zone);

// #sec-temporal-preparepartialtemporalfields
V8_WARN_UNUSED_RESULT MaybeHandle<JSObject> PreparePartialTemporalFields(
    Isolate* isolate, Handle<JSReceiver> fields,
    Handle<FixedArray> field_names);

// #sec-temporal-interprettemporaldatetimefields
V8_WARN_UNUSED_RESULT Maybe<DateTimeRecord> InterpretTemporalDateTimeFields(
    Isolate* isolate, Handle<JSReceiver> calendar, Handle<JSReceiver> fields,
    Handle<JSReceiver> options, const char* method);

V8_WARN_UNUSED_RESULT Maybe<TimeRecord> ToTemporalTimeRecord(
    Isolate* isolate, Handle<JSReceiver> fields, const char* method);

// #sec-temporal-totemporaldurationrecord
V8_WARN_UNUSED_RESULT Maybe<DurationRecord> ToTemporalDurationRecord(
    Isolate* isolate, Handle<JSReceiver> temporal_duration_like,
    const char* method);

// #sec-temporal-tolimitedtemporalduration
V8_WARN_UNUSED_RESULT Maybe<DurationRecord> ToLimitedTemporalDuration(
    Isolate* isolate, Handle<Object> temporal_duration_like,
    std::set<Unit> disallowed_fields, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainDateTime> MoveRelativeDate(
    Isolate* isolate, Handle<JSReceiver> calendar,
    Handle<JSTemporalPlainDateTime> relative_to,
    Handle<JSTemporalDuration> duration, double* result_days,
    const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalZonedDateTime>
MoveRelativeZonedDateTime(Isolate* isolate,
                          Handle<JSTemporalZonedDateTime> zoned_date_time,
                          double years, double months, double weeks,
                          double days, const char* method);

// CreateTemporal*
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainDate> CreateTemporalDate(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    int32_t iso_year, int32_t iso_month, int32_t iso_day,
    Handle<JSReceiver> calendar);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainDate> CreateTemporalDate(
    Isolate* isolate, int32_t iso_year, int32_t iso_month, int32_t iso_day,
    Handle<JSReceiver> calendar);

// #sec-temporal-createtemporaldatetime
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainDateTime>
CreateTemporalDateTime(Isolate* isolate, Handle<JSFunction> target,
                       Handle<HeapObject> new_target, int32_t iso_year,
                       int32_t iso_month, int32_t iso_day, int32_t hour,
                       int32_t minute, int32_t second, int32_t millisecond,
                       int32_t microsecond, int32_t nanosecond,
                       Handle<JSReceiver> calendar);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainDateTime>
CreateTemporalDateTime(Isolate* isolate, int32_t iso_year, int32_t iso_month,
                       int32_t iso_day, int32_t hour, int32_t minute,
                       int32_t second, int32_t millisecond, int32_t microsecond,
                       int32_t nanosecond, Handle<JSReceiver> calendar);

// #sec-temporal-createtemporalmonthday
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainMonthDay>
CreateTemporalMonthDay(Isolate* isolate, Handle<JSFunction> target,
                       Handle<HeapObject> new_target, int32_t iso_month,
                       int32_t iso_day, Handle<JSReceiver> calendar,
                       int32_t reference_iso_year);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainMonthDay>
CreateTemporalMonthDay(Isolate* isolate, int32_t iso_month, int32_t iso_day,
                       Handle<JSReceiver> calendar, int32_t reference_iso_year);

// #sec-temporal-createtemporalplaintime
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainTime> CreateTemporalTime(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    int32_t hour, int32_t minute, int32_t second, int32_t millisecond,
    int32_t microsecond, int32_t nanosecond);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainTime> CreateTemporalTime(
    Isolate* isolate, int32_t hour, int32_t minute, int32_t second,
    int32_t millisecond, int32_t microsecond, int32_t nanosecond);

// #sec-temporal-createtemporalyearmonth
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainYearMonth>
CreateTemporalYearMonth(Isolate* isolate, Handle<JSFunction> target,
                        Handle<HeapObject> new_target, int32_t iso_year,
                        int32_t iso_month, Handle<JSReceiver> calendar,
                        int32_t reference_iso_day);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainYearMonth>
CreateTemporalYearMonth(Isolate* isolate, int32_t iso_year, int32_t iso_month,
                        Handle<JSReceiver> calendar, int32_t reference_iso_day);

// #sec-temporal-createtemporalzoneddatetime
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalZonedDateTime>
CreateTemporalZonedDateTime(Isolate* isolate, Handle<JSFunction> target,
                            Handle<HeapObject> new_target,
                            Handle<BigInt> epoch_nanoseconds,
                            Handle<JSReceiver> time_zone,
                            Handle<JSReceiver> calendar);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalZonedDateTime>
CreateTemporalZonedDateTime(Isolate* isolate, Handle<BigInt> epoch_nanoseconds,
                            Handle<JSReceiver> time_zone,
                            Handle<JSReceiver> calendar);

// #sec-temporal-createtemporalcalendar
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalCalendar> CreateTemporalCalendar(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<String> identifier);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalCalendar> CreateTemporalCalendar(
    Isolate* isolate, Handle<String> identifier);

// #sec-temporal-createtemporalduration
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalDuration> CreateTemporalDuration(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    double years, double months, double weeks, double days, double hours,
    double minutes, double seconds, double milliseconds, double microseconds,
    double nanoseconds);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalDuration> CreateTemporalDuration(
    Isolate* isolate, double years, double months, double weeks, double days,
    double hours, double minutes, double seconds, double milliseconds,
    double microseconds, double nanoseconds);

// #sec-temporal-createnegatedtemporalduration
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalDuration>
CreateNegatedTemporalDuration(Isolate* isolate,
                              Handle<JSTemporalDuration> duration);

// #sec-temporal-createtemporaltimezone
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<String> identifier);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, const std::string& identifier);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, Handle<String> identifier);
// Special case for UTC, skip string checking
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZoneUTC(
    Isolate* isolate);

// #sec-temporal-systeminstant
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalInstant> SystemInstant(
    Isolate* isolate);

// #sec-temporal-systemtimezone
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalTimeZone> SystemTimeZone(
    Isolate* isolate);

V8_WARN_UNUSED_RESULT MaybeHandle<BigInt> SystemUTCEpochNanoseconds(
    Isolate* isolate);

// #sec-temporal-builtintimezonegetinstantfor
V8_WARN_UNUSED_RESULT
MaybeHandle<JSTemporalInstant> BuiltinTimeZoneGetInstantFor(
    Isolate* isolate, Handle<JSReceiver> time_zone,
    Handle<JSTemporalPlainDateTime> date_time, Disambiguation disambiguation);

// ToTemporal*
//
V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainDate> ToTemporalDate(
    Isolate* isolate, Handle<Object> item, Handle<JSReceiver> options,
    const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainDate> ToTemporalDate(
    Isolate* isolate, Handle<Object> item, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainDateTime> ToTemporalDateTime(
    Isolate* isolate, Handle<Object> item, Handle<JSReceiver> options,
    const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainDateTime> ToTemporalDateTime(
    Isolate* isolate, Handle<Object> item, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainTime> ToTemporalTime(
    Isolate* isolate, Handle<Object> item, ShowOverflow overflow,
    const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainTime> ToTemporalTime(
    Isolate* isolate, Handle<Object> item, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainMonthDay> ToTemporalMonthDay(
    Isolate* isolate, Handle<Object> item, Handle<JSReceiver> options,
    const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainMonthDay> ToTemporalMonthDay(
    Isolate* isolate, Handle<Object> item, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainYearMonth> ToTemporalYearMonth(
    Isolate* isolate, Handle<Object> item, Handle<JSReceiver> options,
    const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainYearMonth> ToTemporalYearMonth(
    Isolate* isolate, Handle<Object> item, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalZonedDateTime>
ToTemporalZonedDateTime(Isolate* isolate, Handle<Object> item,
                        Handle<JSReceiver> options, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalZonedDateTime>
ToTemporalZonedDateTime(Isolate* isolate, Handle<Object> item,
                        const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSReceiver> ToTemporalCalendar(
    Isolate* isolate, Handle<Object> item, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSReceiver> ToTemporalCalendarWithISODefault(
    Isolate* isolate, Handle<Object> item, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalInstant> ToTemporalInstant(
    Isolate* isolate, Handle<Object> item, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalDuration> ToTemporalDuration(
    Isolate* isolate, Handle<Object> item, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSReceiver> ToTemporalTimeZone(
    Isolate* isolate, Handle<Object> item, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainDateTime> SystemDateTime(
    Isolate* isolate, Handle<Object> temporal_time_zone_like,
    Handle<Object> calendar_like, const char* method);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalZonedDateTime> SystemZonedDateTime(
    Isolate* isolate, Handle<Object> temporal_time_zone_like,
    Handle<Object> calendar_like, const char* method);

// #sec-temporal-compareepochnanoseconds
V8_WARN_UNUSED_RESULT MaybeHandle<Smi> CompareEpochNanoseconds(
    Isolate* isolate, Handle<BigInt> one, Handle<BigInt> two);

V8_WARN_UNUSED_RESULT MaybeHandle<Object> ToRelativeTemporalObject(
    Isolate* isolate, Handle<JSReceiver> options, const char* method);

// String formatting

V8_WARN_UNUSED_RESULT MaybeHandle<String> TemporalDurationToString(
    Isolate* isolate, const DurationRecord& dur, Precision precision);

V8_WARN_UNUSED_RESULT MaybeHandle<String> TemporalDateToString(
    Isolate* isolate, int32_t iso_year, int32_t iso_month, int32_t iso_day,
    Handle<JSReceiver> calendar, ShowCalendar show_calendar);

V8_WARN_UNUSED_RESULT MaybeHandle<String> TemporalTimeToString(
    Isolate* isolate, int32_t hour, int32_t minute, int32_t second,
    int32_t millisecond, int32_t microsecond, int32_t nanosecond,
    Precision precision);

V8_WARN_UNUSED_RESULT MaybeHandle<String> TemporalDateTimeToString(
    Isolate* isolate, int32_t iso_year, int32_t iso_month, int32_t iso_day,
    int32_t hour, int32_t minute, int32_t second, int32_t millisecond,
    int32_t microsecond, int32_t nanosecond, Handle<JSReceiver> calendar,
    Precision precision, ShowCalendar show_calendar);

V8_WARN_UNUSED_RESULT MaybeHandle<String> TemporalYearMonthToString(
    Isolate* isolate, int32_t iso_year, int32_t iso_month, int32_t iso_day,
    Handle<JSReceiver> calendar, ShowCalendar show_calendar);

V8_WARN_UNUSED_RESULT MaybeHandle<String> TemporalMonthDayToString(
    Isolate* isolate, int32_t iso_year, int32_t iso_month, int32_t iso_day,
    Handle<JSReceiver> calendar, ShowCalendar show_calendar);

// #sec-temporal-temporalzoneddatetimetostring
V8_WARN_UNUSED_RESULT MaybeHandle<String> TemporalZonedDateTimeToString(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Precision precision, ShowCalendar show_calendar,
    ShowTimeZone show_time_zone, ShowOffset show_offset, double increment,
    Unit unit, RoundingMode rounding_mode);

// #sec-temporal-temporalzoneddatetimetostring
V8_WARN_UNUSED_RESULT MaybeHandle<String> TemporalZonedDateTimeToString(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Precision precision, ShowCalendar show_calendar,
    ShowTimeZone show_time_zone, ShowOffset show_offset);

V8_WARN_UNUSED_RESULT MaybeHandle<String> TemporalInstantToString(
    Isolate* isolate, Handle<JSTemporalInstant> handle,
    Handle<Object> time_zone, Precision precision);

V8_WARN_UNUSED_RESULT std::string FormatTimeZoneOffsetString(
    int64_t offset_nanoseconds);

// #sec-temporal-builtintimezonegetoffsetstringfor
V8_WARN_UNUSED_RESULT MaybeHandle<String> BuiltinTimeZoneGetOffsetStringFor(
    Isolate* isolate, Handle<JSReceiver> time_zone,
    Handle<JSTemporalInstant> instant);

V8_WARN_UNUSED_RESULT Maybe<std::string> BuiltinTimeZoneGetOffsetStdStringFor(
    Isolate* isolate, Handle<JSReceiver> time_zone,
    Handle<JSTemporalInstant> instant);

// ISO8601 String Parsing

// #sec-temporal-parsetemporalzoneddatetimestring
V8_WARN_UNUSED_RESULT Maybe<ZonedDateTimeRecord>
ParseTemporalZonedDateTimeString(Isolate* isolate, Handle<String> iso_string);

// #sec-temporal-parsetemporalcalendarstring
V8_WARN_UNUSED_RESULT Maybe<std::string> ParseTemporalCalendarString(
    Isolate* isolate, Handle<String> iso_string);

// #sec-temporal-parsetemporaldatestring
V8_WARN_UNUSED_RESULT Maybe<DateRecord> ParseTemporalDateString(
    Isolate* isolate, Handle<String> iso_string);

// #sec-temporal-parsetemporaldatetimestring
V8_WARN_UNUSED_RESULT Maybe<DateTimeRecord> ParseTemporalDateTimeString(
    Isolate* isolate, Handle<String> iso_string);
// #sec-temporal-parsetemporaldurationstring
V8_WARN_UNUSED_RESULT Maybe<DurationRecord> ParseTemporalDurationString(
    Isolate* isolate, Handle<String> iso_string);

// #sec-temporal-parsetemporalmonthdaystring
V8_WARN_UNUSED_RESULT Maybe<DateRecord> ParseTemporalMonthDayString(
    Isolate* isolate, Handle<String> iso_string);

// #sec-temporal-parsetemporaltimestring
V8_WARN_UNUSED_RESULT Maybe<TimeRecord> ParseTemporalTimeString(
    Isolate* isolate, Handle<String> iso_string);

// #sec-temporal-parsetemporaltimezonestring
V8_WARN_UNUSED_RESULT Maybe<TimeZoneRecord> ParseTemporalTimeZoneString(
    Isolate* isolate, Handle<String> iso_string);

// #sec-temporal-parsetemporaltimezone
V8_WARN_UNUSED_RESULT Maybe<std::string> ParseTemporalTimeZone(
    Isolate* isolate, Handle<String> string);

V8_WARN_UNUSED_RESULT Maybe<int64_t> ParseTimeZoneOffsetString(
    Isolate* isolate, Handle<String> offset_string,
    bool throwIfNotSatisfy = true);

void BalanceISODate(Isolate* isolate, int32_t* year, int32_t* month,
                    int32_t* day);
// #sec-temporal-parsetemporalrelativetostring
V8_WARN_UNUSED_RESULT Maybe<ZonedDateTimeRecord> ParseTemporalRelativeToString(
    Isolate* isolate, Handle<String> string);

// #sec-temporal-parsetemporalyearmonthstring
V8_WARN_UNUSED_RESULT Maybe<DateRecord> ParseTemporalYearMonthString(
    Isolate* isolate, Handle<String> iso_string);
V8_WARN_UNUSED_RESULT MaybeHandle<BigInt> ParseTemporalInstant(
    Isolate* isolate, Handle<String> iso_string);

bool IsValidTimeZoneNumericUTCOffsetString(Isolate* isolate,
                                           Handle<String> identifier);

// Math and Misc
V8_WARN_UNUSED_RESULT Maybe<bool> DifferenceISODate(
    Isolate* isolate, int32_t y1, int32_t m1, int32_t d1, int32_t y2,
    int32_t m2, int32_t d2, Unit largest_unit, double* years, double* months,
    double* weeks, double* days, const char* method);

// #sec-temporal-regulateisodate
V8_WARN_UNUSED_RESULT Maybe<bool> RegulateISODate(Isolate* isolate,
                                                  int32_t* year, int32_t* month,
                                                  int32_t* day,
                                                  ShowOverflow overflow);
V8_WARN_UNUSED_RESULT Maybe<bool> RegulateTime(Isolate* isolate, int32_t* hour,
                                               int32_t* minute, int32_t* second,
                                               int32_t* millisecond,
                                               int32_t* microsecond,
                                               int32_t* nanosecond,
                                               ShowOverflow overflow);

V8_WARN_UNUSED_RESULT MaybeHandle<BigInt> AddInstant(
    Isolate* isolate, Handle<BigInt> epoch_nanoseconds, int32_t hours,
    int32_t minutes, int32_t seconds, int32_t milliseconds,
    int32_t microseconds, int32_t nanoseconds);

// #sec-temporal-addisodate
Maybe<bool> AddISODate(Isolate* isolate, int32_t year, int32_t month,
                       int32_t day, double years, double months, double weeks,
                       double days, ShowOverflow overflow, int32_t* out_year,
                       int32_t* out_month, int32_t* out_day);

// #sec-temporal-daysuntil
V8_WARN_UNUSED_RESULT Maybe<double> DaysUntil(Isolate* isolate,
                                              Handle<Object> earlier,
                                              Handle<Object> later,
                                              const char* method);

void BalanceISODate(Isolate* isolate, int32_t* year, int32_t* month,
                    int32_t* day);

// #sec-temporal-balanceduration
V8_WARN_UNUSED_RESULT Maybe<bool> BalanceDuration(
    Isolate* isolate, double* days, double* hours, double* minutes,
    double* seconds, double* milliseconds, double* microseconds,
    double* nanoseconds, Unit largest_unit);

// #sec-temporal-balancedurationrelative
V8_WARN_UNUSED_RESULT Maybe<bool> BalanceDurationRelative(
    Isolate* isolate, double* years, double* months, double* weeks,
    double* days, Unit largest_unit, Handle<Object> relative_to,
    const char* method);

// #sec-temporal-unbalancedurationrelative
V8_WARN_UNUSED_RESULT Maybe<bool> UnbalanceDurationRelative(
    Isolate* isolate, double* years, double* months, double* weeks,
    double* days, Unit largest_unit, Handle<Object> relative_to,
    const char* method);

// #sec-temporal-balanceduration
V8_WARN_UNUSED_RESULT Maybe<bool> BalanceDuration(
    Isolate* isolate, double* days, double* hours, double* minutes,
    double* seconds, double* milliseconds, double* microseconds,
    double* nanoseconds, Unit largest_unit, Handle<Object> relative_to);

// #sec-temporal-adjustroundeddurationdays
V8_WARN_UNUSED_RESULT Maybe<DurationRecord> AdjustRoundedDurationDays(
    Isolate* isolate, const DurationRecord& duration, double increment,
    Unit unit, RoundingMode rounding_mode, Handle<Object> relative_to);

V8_WARN_UNUSED_RESULT Maybe<DurationRecord> DifferenceISODateTime(
    Isolate* isolate, double y1, double mon1, double d1, double h1, double min1,
    double s1, double ms1, double mus1, double ns1, double y2, double mon2,
    double d2, double h2, double min2, double s2, double ms2, double mus2,
    double ns2, Handle<JSReceiver> calendar, Unit largest_unit,
    Handle<Object> relative_to);

// #sec-temporal-adddatetime
V8_WARN_UNUSED_RESULT Maybe<DateTimeRecordCommon> AddDateTime(
    Isolate* isolate, int32_t year, int32_t month, int32_t day, int32_t hour,
    int32_t minute, int32_t second, int32_t millisecond, int32_t microsecond,
    int32_t nanosecond, Handle<JSReceiver> calendar, const DurationRecord& dur,
    Handle<Object> options);

V8_WARN_UNUSED_RESULT Maybe<DurationRecord> AddDuration(
    Isolate* isolate, const DurationRecord& dur1, const DurationRecord& dur2,
    Handle<Object> relative_to);

// #sec-temporal-addzoneddatetime
V8_WARN_UNUSED_RESULT MaybeHandle<BigInt> AddZonedDateTime(
    Isolate* isolate, Handle<BigInt> eopch_nanoseconds,
    Handle<JSReceiver> time_zone, Handle<JSReceiver> calendar,
    const DurationRecord& duration);

V8_WARN_UNUSED_RESULT MaybeHandle<BigInt> AddZonedDateTime(
    Isolate* isolate, Handle<BigInt> eopch_nanoseconds,
    Handle<JSReceiver> time_zone, Handle<JSReceiver> calendar,
    const DurationRecord& duration, Handle<JSReceiver> options);

// #sec-temporal-differenceinstant
V8_WARN_UNUSED_RESULT MaybeHandle<BigInt> DifferenceInstant(
    Isolate* isolate, Handle<BigInt> ns1, Handle<BigInt> ns2, double increment,
    Unit unit, RoundingMode rounding_mode);

// #sec-temporal-differencezoneddatetime
V8_WARN_UNUSED_RESULT Maybe<DurationRecord> DifferenceZonedDateTime(
    Isolate* isolate, Handle<BigInt> ns1, Handle<BigInt> ns2,
    Handle<JSReceiver> time_zone, Handle<JSReceiver> calendar,
    Unit largest_unit, Handle<Object> options);

// #sec-temporal-isvalidepochnanoseconds
bool IsValidEpochNanoseconds(Isolate* isolate,
                             Handle<BigInt> epoch_nanoseconds);

// #sec-temporal-isvalidduration
bool IsValidDuration(Isolate* isolate, const DurationRecord& dur);

// #sec-temporal-nanosecondstodays
V8_WARN_UNUSED_RESULT Maybe<bool> NanosecondsToDays(
    Isolate* isolate, Handle<BigInt> nanoseconds,
    Handle<Object> relative_to_obj, double* result_days,
    double* resultj_nanoseconds, double* result_day_length);

V8_WARN_UNUSED_RESULT Maybe<bool> NanosecondsToDays(
    Isolate* isolate, double nanoseconds, Handle<Object> relative_to_obj,
    double* result_days, double* resultj_nanoseconds,
    double* result_day_length);

// #sec-temporal-roundtemporalinstant
V8_WARN_UNUSED_RESULT MaybeHandle<BigInt> RoundTemporalInstant(
    Isolate* isolate, Handle<BigInt> ns, double increment, Unit unit,
    RoundingMode rounding_mode);

// #sec-temporal-calculateoffsetshift
V8_WARN_UNUSED_RESULT Maybe<int64_t> CalculateOffsetShift(
    Isolate* isolate, Handle<Object> relative_to, const DurationRecord& dur);

// #sec-temporal-roundduration
V8_WARN_UNUSED_RESULT Maybe<DurationRecord> RoundDuration(
    Isolate* isolate, const DurationRecord& dur, double increment, Unit unit,
    RoundingMode rounding_mode, double* remainder, const char* method);

V8_WARN_UNUSED_RESULT Maybe<DurationRecord> RoundDuration(
    Isolate* isolate, const DurationRecord& dur, double increment, Unit unit,
    RoundingMode rounding_mode, Handle<Object> relative_to, double* remainder,
    const char* method);

// #sec-temporal-roundnumbertoincrement
double RoundNumberToIncrement(Isolate* isolate, double x, double increment,
                              RoundingMode rounding_mode);

V8_WARN_UNUSED_RESULT MaybeHandle<BigInt> RoundNumberToIncrement(
    Isolate* isolate, Handle<BigInt> x, double increment,
    RoundingMode rounding_mode);

// #sec-temporal-roundtime
V8_WARN_UNUSED_RESULT DateTimeRecordCommon
RoundTime(Isolate* isolate, int32_t hour, int32_t minute, int32_t second,
          int32_t millisecond, int32_t microsecond, int32_t nanosecond,
          double increment, Unit unit, RoundingMode rounding_mode,
          double day_length_ns);
V8_WARN_UNUSED_RESULT DateTimeRecordCommon
RoundTime(Isolate* isolate, int32_t hour, int32_t minute, int32_t second,
          int32_t millisecond, int32_t microsecond, int32_t nanosecond,
          double increment, Unit unit, RoundingMode rounding_mode);
// #sec-temporal-roundisodatetime
V8_WARN_UNUSED_RESULT DateTimeRecordCommon RoundISODateTime(
    Isolate* isolate, int32_t year, int32_t month, int32_t day, int32_t hour,
    int32_t minute, int32_t second, int32_t millisecond, int32_t microsecond,
    int32_t nanosecond, double increment, Unit unit, RoundingMode rounding_mode,
    double day_length_ns);
V8_WARN_UNUSED_RESULT DateTimeRecordCommon
RoundISODateTime(Isolate* isolate, int32_t year, int32_t month, int32_t day,
                 int32_t hour, int32_t minute, int32_t second,
                 int32_t millisecond, int32_t microsecond, int32_t nanosecond,
                 double increment, Unit unit, RoundingMode rounding_mode);

// #sec-temporal-interpretisodatetimeoffset
enum class OffsetBehaviour { kOption, kExact, kWall };
V8_WARN_UNUSED_RESULT
MaybeHandle<BigInt> InterpretISODateTimeOffset(
    Isolate* isolate, double year, double month, double day, double hour,
    double minute, double second, double millisecond, double microsecond,
    double nanosecond, OffsetBehaviour offset_behaviour, int64_t offset_ns,
    Handle<JSReceiver> time_zone, Disambiguation disambiguation, Offset offset,
    const char* method);

V8_WARN_UNUSED_RESULT
MaybeHandle<BigInt> GetEpochFromISOParts(Isolate* isolate, int32_t year,
                                         int32_t month, int32_t day,
                                         int32_t hour, int32_t minute,
                                         int32_t second, int32_t millisecond,
                                         int32_t microsecond,
                                         int32_t nanosecond);

int32_t DurationSign(Isolate* isolaet, const DurationRecord& dur);

// #sec-temporal-isvalidduration
bool IsValidDuration(Isolate* isolate, const DurationRecord& dur);

// #sec-temporal-isisoleapyear
bool IsISOLeapYear(Isolate* isolate, int32_t year);

// #sec-temporal-isodaysinmonth
int32_t ISODaysInMonth(Isolate* isolate, int32_t year, int32_t month);

// #sec-temporal-isodaysinyear
int32_t ISODaysInYear(Isolate* isolate, int32_t year);

// #sec-temporal-toisodayofweek
int32_t ToISODayOfWeek(Isolate* isolate, int32_t year, int32_t month,
                       int32_t day);

// #sec-temporal-toisodayofyear
int32_t ToISODayOfYear(Isolate* isolate, int32_t year, int32_t month,
                       int32_t day);

// #sec-temporal-toisoweekofyear
int32_t ToISOWeekOfYear(Isolate* isolate, int32_t year, int32_t month,
                        int32_t day);

bool IsValidTime(Isolate* isolate, int32_t hour, int32_t minute, int32_t second,
                 int32_t millisecond, int32_t microsecond, int32_t nanosecond);

// #sec-temporal-isvalidisodate
bool IsValidISODate(Isolate* isolate, int32_t year, int32_t month, int32_t day);

bool IsValidISOMonth(Isolate* isolate, int32_t month);

// #sec-temporal-compareisodate
int32_t CompareISODate(Isolate* isolate, int32_t y1, int32_t m1, int32_t d1,
                       int32_t y2, int32_t m2, int32_t d2);

// #sec-temporal-comparetemporaltime
int32_t CompareTemporalTime(Isolate* isolate, int32_t h1, int32_t min1,
                            int32_t s1, int32_t ms1, int32_t mus1, int32_t ns1,
                            int32_t h2, int32_t min2, int32_t s2, int32_t ms2,
                            int32_t mus2, int32_t ns2);

// #sec-temporal-compareisodatetime
int32_t CompareISODateTime(Isolate* isolate, int32_t y1, int32_t mon1,
                           int32_t d1, int32_t h1, int32_t min1, int32_t s1,
                           int32_t ms1, int32_t mus1, int32_t ns1, int32_t y2,
                           int32_t mon2, int32_t d2, int32_t h2, int32_t min2,
                           int32_t s2, int32_t ms2, int32_t mus2, int32_t ns2);

// #sec-temporal-balanceisoyearmonth
void BalanceISOYearMonth(Isolate* isolate, int32_t* year, int32_t* month);

// #sec-temporal-balancetime
V8_WARN_UNUSED_RESULT DateTimeRecordCommon
BalanceTime(Isolate* isolate, int64_t hour, int64_t minute, int64_t second,
            int64_t millisecond, int64_t microsecond, int64_t nanosecond);

// #sec-temporal-differencetime
V8_WARN_UNUSED_RESULT DurationRecord
DifferenceTime(Isolate* isolate, int32_t h1, int32_t min1, int32_t s1,
               int32_t ms1, int32_t mus1, int32_t ns1, int32_t h2, int32_t min2,
               int32_t s2, int32_t ms2, int32_t mus2, int32_t ns2);

// #sec-temporal-addtime
V8_WARN_UNUSED_RESULT DateTimeRecordCommon
AddTime(Isolate* isolate, int32_t hour, int32_t minute, int32_t second,
        int32_t millisecond, int32_t microsecond, int32_t nanosecond,
        int32_t hours, int32_t minutes, int32_t seconds, int32_t milliseconds,
        int32_t microseconds, int32_t nanoseconds);

// #sec-temporal-totaldurationnanoseconds
double TotalDurationNanoseconds(Isolate* isolate, double days, double hours,
                                double minutes, double seconds,
                                double milliseconds, double microseconds,
                                double nanoseconds, int64_t offset_shift);

// Calendar Operations

MaybeHandle<JSReceiver> DefaultMergeFields(
    Isolate* isolate, Handle<JSReceiver> fields,
    Handle<JSReceiver> additional_fields);

V8_WARN_UNUSED_RESULT MaybeHandle<FixedArray> CalendarFields(
    Isolate* isolate, Handle<JSReceiver> calendar, Handle<FixedArray> fields);

V8_WARN_UNUSED_RESULT MaybeHandle<JSReceiver> CalendarMergeFields(
    Isolate* isolate, Handle<JSReceiver> calendar, Handle<JSReceiver> fields,
    Handle<JSReceiver> additional_fields);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainDate> CalendarDateAdd(
    Isolate* isolate, Handle<JSReceiver> calendar, Handle<Object> date,
    Handle<Object> durations, Handle<Object> options, Handle<Object> date_add);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlainDate> CalendarDateAdd(
    Isolate* isolate, Handle<JSReceiver> calendar, Handle<Object> date,
    Handle<Object> durations, Handle<Object> options);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalDuration> CalendarDateUntil(
    Isolate* isolate, Handle<JSReceiver> calendar, Handle<Object> one,
    Handle<Object> two, Handle<Object> options, Handle<Object> date_until);

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalDuration> CalendarDateUntil(
    Isolate* isolate, Handle<JSReceiver> calendar, Handle<Object> one,
    Handle<Object> two, Handle<Object> options);

#define DECLARE_FROM_FIELDS_ABSTRACT_OPERATION(Name)                         \
  V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalPlain##Name> Name##FromFields( \
      Isolate* isolate, Handle<JSReceiver> calendar,                         \
      Handle<JSReceiver> fields, Handle<Object> options);
DECLARE_FROM_FIELDS_ABSTRACT_OPERATION(Date)
DECLARE_FROM_FIELDS_ABSTRACT_OPERATION(YearMonth)
DECLARE_FROM_FIELDS_ABSTRACT_OPERATION(MonthDay)
#undef DECLARE_FROM_FIELDS_ABSTRACT_OPERATION

// #sec-temporal-calendarequals
V8_WARN_UNUSED_RESULT MaybeHandle<Oddball> CalendarEquals(
    Isolate* isolate, Handle<JSReceiver> one, Handle<JSReceiver> two);

// #sec-temporal-consolidatecalendars
V8_WARN_UNUSED_RESULT MaybeHandle<JSReceiver> ConsolidateCalendars(
    Isolate* isolate, Handle<JSReceiver> one, Handle<JSReceiver> two);

// #sec-temporal-getoffsetnanosecondsfor
V8_WARN_UNUSED_RESULT Maybe<int64_t> GetOffsetNanosecondsFor(
    Isolate* isolate, Handle<JSReceiver> time_zone, Handle<Object> instant);

bool IsBuiltinCalendar(Isolate* isolate, Handle<String> id);
bool IsBuiltinCalendar(Isolate* isolate, const std::string& id);

// Internal Helper Function
Handle<String> CalendarIdentifier(Isolate* isolate, int32_t index);
int32_t CalendarIndex(Isolate* isolate, Handle<String> id);

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

V8_WARN_UNUSED_RESULT Maybe<bool> TimeZoneEquals(Isolate* isolate,
                                                 Handle<Object> one,
                                                 Handle<Object> two);

inline double R(double d) { return static_cast<int64_t>(d); }
inline double FLOOR(double d) { return static_cast<int64_t>(d); }
inline double MODULO(double a, int64_t b) {
  return static_cast<int64_t>(a) % b;
}

V8_WARN_UNUSED_RESULT MaybeHandle<JSTemporalInstant>
DisambiguatePossibleInstants(Isolate* isolate,
                             Handle<FixedArray> possible_instants,
                             Handle<JSReceiver> time_zone,
                             Handle<Object> date_time,
                             Disambiguation disambiguation);

V8_WARN_UNUSED_RESULT MaybeHandle<FixedArray> GetPossibleInstantsFor(
    Isolate* isolate, Handle<JSReceiver> time_zone, Handle<Object> date_time);

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)

#ifdef DEBUG
#define TEMPORAL_DEBUG_INFO AT
#define TEMPORAL_ENTER_FUNC
//#define TEMPORAL_ENTER_FUNC  { printf("Start: %s\n", __func__); }
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
  TEMPORAL_ENTER_FUNC
  return Intl::DefaultTimeZone(isolate);
}
#else   //  V8_INTL_SUPPORT
MaybeHandle<String> DefaultTimeZone(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC
  // For now, always return "UTC"
  return isolate->factory()->UTC_string();
}
#endif  // V8_INTL_SUPPORT

// #sec-temporal-isodatetimewithinlimits
bool ISODateTimeWithinLimits(Isolate* isolate, int32_t year, int32_t month,
                             int32_t day, int32_t hour, int32_t minute,
                             int32_t second, int32_t millisecond,
                             int32_t microsecond, int32_t nanosecond) {
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
  // 1. Assert: ! IsBuiltinCalendar(identifier) is true.
  // 2. If newTarget is not provided, set newTarget to %Temporal.Calendar%.
  // 3. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.Calendar.prototype%", « [[InitializedTemporalCalendar]],
  // [[Identifier]] »).
  int32_t index = CalendarIndex(isolate, identifier);
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalCalendar)

  DisallowGarbageCollection no_gc;
  object->set_flags(0);
  // 4. Set object.[[Identifier]] to identifier.
  object->set_calendar_index(index);
  // 5. Return object.
  return object;
}

MaybeHandle<JSTemporalCalendar> CreateTemporalCalendar(
    Isolate* isolate, Handle<String> identifier) {
  TEMPORAL_ENTER_FUNC
  return CreateTemporalCalendar(isolate, CONSTRUCTOR(calendar),
                                CONSTRUCTOR(calendar), identifier);
}

// #sec-temporal-createtemporaldate
MaybeHandle<JSTemporalPlainDate> CreateTemporalDate(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    int32_t iso_year, int32_t iso_month, int32_t iso_day,
    Handle<JSReceiver> calendar) {
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
  return CreateTemporalTime(isolate, CONSTRUCTOR(plain_time),
                            CONSTRUCTOR(plain_time), hour, minute, second,
                            millisecond, microsecond, nanosecond);
}

MaybeHandle<JSTemporalPlainMonthDay> CreateTemporalMonthDay(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    int32_t iso_month, int32_t iso_day, Handle<JSReceiver> calendar,
    int32_t reference_iso_year) {
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
  return CreateTemporalMonthDay(isolate, CONSTRUCTOR(plain_month_day),
                                CONSTRUCTOR(plain_month_day), iso_month,
                                iso_day, calendar, reference_iso_year);
}

// #sec-temporal-createtemporalyearmonth
MaybeHandle<JSTemporalPlainYearMonth> CreateTemporalYearMonth(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    int32_t iso_year, int32_t iso_month, Handle<JSReceiver> calendar,
    int32_t reference_iso_day) {
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
  return CreateTemporalYearMonth(isolate, CONSTRUCTOR(plain_year_month),
                                 CONSTRUCTOR(plain_year_month), iso_year,
                                 iso_month, calendar, reference_iso_day);
}

// #sec-temporal-createtemporalzoneddatetime
MaybeHandle<JSTemporalZonedDateTime> CreateTemporalZonedDateTime(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<BigInt> epoch_nanoseconds, Handle<JSReceiver> time_zone,
    Handle<JSReceiver> calendar) {
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
  Factory* factory = isolate->factory();
  // 1. If ! IsValidDuration(years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds) is false, throw a
  // RangeError exception.
  if (!IsValidDuration(isolate,
                       {years, months, weeks, days, hours, minutes, seconds,
                        milliseconds, microseconds, nanoseconds})) {
    THROW_INVALID_RANGE(JSTemporalDuration);
  }

  Handle<Object> years_obj = factory->NewNumber(std::floor(years));
  Handle<Object> months_obj = factory->NewNumber(std::floor(months));
  Handle<Object> weeks_obj = factory->NewNumber(std::floor(weeks));
  Handle<Object> days_obj = factory->NewNumber(std::floor(days));
  Handle<Object> hours_obj = factory->NewNumber(std::floor(hours));
  Handle<Object> minutes_obj = factory->NewNumber(std::floor(minutes));
  Handle<Object> seconds_obj = factory->NewNumber(std::floor(seconds));
  Handle<Object> milliseconds_obj =
      factory->NewNumber(std::floor(milliseconds));
  Handle<Object> microseconds_obj =
      factory->NewNumber(std::floor(microseconds));
  Handle<Object> nanoseconds_obj = factory->NewNumber(std::floor(nanoseconds));

  // 2. If newTarget is not present, set it to %Temporal.Duration%.
  // 3. Let object be ? OrdinaryCreateFromConstructor(newTarget,
  // "%Temporal.Duration.prototype%", « [[InitializedTemporalDuration]],
  // [[Years]], [[Months]], [[Weeks]], [[Days]], [[Hours]], [[Minutes]],
  // [[Seconds]], [[Milliseconds]], [[Microseconds]], [[Nanoseconds]] »).
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalDuration)
  DisallowGarbageCollection no_gc;
  // 4. Set object.[[Years]] to years.
  object->set_years(*years_obj);
  // 5. Set object.[[Months]] to months.
  object->set_months(*months_obj);
  // 6. Set object.[[Weeks]] to weeks.
  object->set_weeks(*weeks_obj);
  // 7. Set object.[[Days]] to days.
  object->set_days(*days_obj);
  // 8. Set object.[[Hours]] to hours.
  object->set_hours(*hours_obj);
  // 9. Set object.[[Minutes]] to minutes.
  object->set_minutes(*minutes_obj);
  // 10. Set object.[[Seconds]] to seconds.
  object->set_seconds(*seconds_obj);
  // 11. Set object.[[Milliseconds]] to milliseconds.
  object->set_milliseconds(*milliseconds_obj);
  // 12. Set object.[[Microseconds]] to microseconds.
  object->set_microseconds(*microseconds_obj);
  // 13. Set object.[[Nanoseconds]] to nanoseconds.
  object->set_nanoseconds(*nanoseconds_obj);
  // 14. Return object.
  return object;
}

MaybeHandle<JSTemporalDuration> CreateTemporalDuration(
    Isolate* isolate, double years, double months, double weeks, double days,
    double hours, double minutes, double seconds, double milliseconds,
    double microseconds, double nanoseconds) {
  TEMPORAL_ENTER_FUNC
  return CreateTemporalDuration(isolate, CONSTRUCTOR(duration),
                                CONSTRUCTOR(duration), years, months, weeks,
                                days, hours, minutes, seconds, milliseconds,
                                microseconds, nanoseconds);
}

// #sec-temporal-createnegatedtemporalduration
MaybeHandle<JSTemporalDuration> CreateNegatedTemporalDuration(
    Isolate* isolate, Handle<JSTemporalDuration> duration) {
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
  return CreateTemporalInstant(isolate, CONSTRUCTOR(instant),
                               CONSTRUCTOR(instant), epoch_nanoseconds);
}

namespace {

MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZoneFromIndex(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    int32_t index) {
  TEMPORAL_ENTER_FUNC
  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalTimeZone)
  DisallowGarbageCollection no_gc;
  object->set_flags(0);
  object->set_details(0);

  object->set_is_offset(false);
  object->set_offset_milliseconds_or_time_zone_index(index);
  return object;
}

MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZoneUTC(
    Isolate* isolate, Handle<JSFunction> target,
    Handle<HeapObject> new_target) {
  TEMPORAL_ENTER_FUNC
  return CreateTemporalTimeZoneFromIndex(isolate, target, new_target, 0);
}

MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    const std::string& identifier) {
  TEMPORAL_ENTER_FUNC
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

  ORDINARY_CREATE_FROM_CONSTRUCTOR(object, target, new_target,
                                   JSTemporalTimeZone)
  DisallowGarbageCollection no_gc;
  object->set_flags(0);
  object->set_details(0);

  object->set_is_offset(true);
  object->set_offset_nanoseconds(offset_nanoseconds);
  return object;
}
}  // namespace

// #sec-temporal-createtemporaltimezone
MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<String> identifier) {
  TEMPORAL_ENTER_FUNC
  return CreateTemporalTimeZone(isolate, target, new_target,
                                identifier->ToCString().get());
}

MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, const std::string& identifier) {
  TEMPORAL_ENTER_FUNC
  return CreateTemporalTimeZone(isolate, CONSTRUCTOR(time_zone),
                                CONSTRUCTOR(time_zone), identifier);
}

MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZone(
    Isolate* isolate, Handle<String> identifier) {
  TEMPORAL_ENTER_FUNC
  return CreateTemporalTimeZone(isolate, CONSTRUCTOR(time_zone),
                                CONSTRUCTOR(time_zone), identifier);
}

MaybeHandle<JSTemporalTimeZone> CreateTemporalTimeZoneUTC(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC
  return CreateTemporalTimeZoneUTC(isolate, CONSTRUCTOR(time_zone),
                                   CONSTRUCTOR(time_zone));
}

// #sec-temporal-systeminstant
MaybeHandle<JSTemporalInstant> SystemInstant(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC
  // 1. Let ns be ! SystemUTCEpochNanoseconds().
  Handle<BigInt> ns;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, ns, SystemUTCEpochNanoseconds(isolate),
                             JSTemporalInstant);
  // 2. Return ? CreateTemporalInstant(ns).
  return CreateTemporalInstant(isolate, ns);
}

// #sec-temporal-systemtimezone
MaybeHandle<JSTemporalTimeZone> SystemTimeZone(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC
  Handle<String> default_time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, default_time_zone,
                             DefaultTimeZone(isolate), JSTemporalTimeZone);
  return CreateTemporalTimeZone(isolate, default_time_zone);
}

namespace {

Maybe<DateTimeRecordCommon> GetISOPartsFromEpoch(
    Isolate* isolate, Handle<BigInt> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC
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

// #sec-temporal-balanceisodatetime
DateTimeRecordCommon BalanceISODateTime(Isolate* isolate, int32_t year,
                                        int32_t month, int32_t day,
                                        int32_t hour, int32_t minute,
                                        int32_t second, int32_t millisecond,
                                        int32_t microsecond,
                                        int64_t nanosecond) {
  TEMPORAL_ENTER_FUNC
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
  TEMPORAL_ENTER_FUNC
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

MaybeHandle<FixedArray> GetPossibleInstantsFor(Isolate* isolate,
                                               Handle<JSReceiver> time_zone,
                                               Handle<Object> date_time) {
  TEMPORAL_ENTER_FUNC
  // 1. Let possibleInstants be ? Invoke(timeZone, "getPossibleInstantsFor", «
  // dateTime »).
  Handle<Object> function;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, function,
      Object::GetProperty(isolate, time_zone,
                          isolate->factory()->getPossibleInstantsFor_string()),
      FixedArray);
  if (!function->IsCallable()) {
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kCalledNonCallable,
                     isolate->factory()->getPossibleInstantsFor_string()),
        FixedArray);
  }
  Handle<Object> possible_instants;
  {
    Handle<Object> argv[] = {date_time};
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, possible_instants,
        Execution::Call(isolate, function, time_zone, 1, argv), FixedArray);
  }

  {
    Handle<Object> argv[] = {possible_instants};
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, possible_instants,
        Execution::CallBuiltin(
            isolate, isolate->temporal_instant_fixed_array_from_iterable(),
            possible_instants, 1, argv),
        FixedArray);
  }
  CHECK(possible_instants->IsFixedArray());
  return Handle<FixedArray>::cast(possible_instants);
}

MaybeHandle<JSTemporalInstant> DisambiguatePossibleInstants(
    Isolate* isolate, Handle<FixedArray> possible_instants,
    Handle<JSReceiver> time_zone, Handle<Object> date_time_obj,
    Disambiguation disambiguation) {
  TEMPORAL_ENTER_FUNC
  // 1. Assert: dateTime has an [[InitializedTemporalDateTime]] internal slot.
  CHECK(date_time_obj->IsJSTemporalPlainDateTime());
  Handle<JSTemporalPlainDateTime> date_time =
      Handle<JSTemporalPlainDateTime>::cast(date_time_obj);


  // 2. Let n be possibleInstants's length.
  int32_t n = possible_instants->length();

  // 3. If n = 1, then
  if (n == 1) {
    // a. Return possibleInstants[0].
    Handle<Object> ret_obj = FixedArray::get(*possible_instants, 0, isolate);
    CHECK(ret_obj->IsJSTemporalInstant());
    return Handle<JSTemporalInstant>::cast(ret_obj);
    // 4. If n ≠ 0, then
  } else if (n != 0) {
    // a. If disambiguation is "earlier" or "compatible", then
    if (disambiguation == Disambiguation::kEarlier ||
        disambiguation == Disambiguation::kCompatible) {
      // i. Return possibleInstants[0].
      Handle<Object> ret_obj = FixedArray::get(*possible_instants, 0, isolate);
      CHECK(ret_obj->IsJSTemporalInstant());
      return Handle<JSTemporalInstant>::cast(ret_obj);
    }
    // b. If disambiguation is "later", then
    if (disambiguation == Disambiguation::kLater) {
      // i. Return possibleInstants[n − 1].
      Handle<Object> ret_obj =
          FixedArray::get(*possible_instants, n - 1, isolate);
      CHECK(ret_obj->IsJSTemporalInstant());
      return Handle<JSTemporalInstant>::cast(ret_obj);
    }
    // c. Assert: disambiguation is "reject".
    CHECK_EQ(disambiguation, Disambiguation::kReject);
    // d. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalInstant);
  }
  // 5. Assert: n = 0.
  CHECK_EQ(n, 0);
  // 6. If disambiguation is "reject", then
  if (disambiguation == Disambiguation::kReject) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalInstant);
  }
  // 7. Let epochNanoseconds be ! GetEpochFromISOParts(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]]).
  Handle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, epoch_nanoseconds,
      GetEpochFromISOParts(
          isolate, date_time->iso_year(), date_time->iso_month(),
          date_time->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
          date_time->iso_second(), date_time->iso_millisecond(),
          date_time->iso_microsecond(), date_time->iso_nanosecond()),
      JSTemporalInstant);

  // 8. Let dayBefore be ! CreateTemporalInstant(epochNanoseconds − 8.64 ×
  // 1013).
  Handle<BigInt> one_day_in_ns = BigInt::FromUint64(isolate, 86400000000000);
  Handle<BigInt> day_before_ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, day_before_ns,
      BigInt::Subtract(isolate, epoch_nanoseconds, one_day_in_ns),
      JSTemporalInstant);
  Handle<JSTemporalInstant> day_before;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, day_before,
                             CreateTemporalInstant(isolate, day_before_ns),
                             JSTemporalInstant);
  // 9. Let dayAfter be ! CreateTemporalInstant(epochNanoseconds + 8.64 × 1013).
  Handle<BigInt> day_after_ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, day_after_ns,
      BigInt::Add(isolate, epoch_nanoseconds, one_day_in_ns),
      JSTemporalInstant);
  Handle<JSTemporalInstant> day_after;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, day_after,
                             CreateTemporalInstant(isolate, day_after_ns),
                             JSTemporalInstant);
  // 10. Let offsetBefore be ? GetOffsetNanosecondsFor(timeZone, dayBefore).
  Maybe<int64_t> maybe_offset_before =
      GetOffsetNanosecondsFor(isolate, time_zone, day_before);
  MAYBE_RETURN(maybe_offset_before, Handle<JSTemporalInstant>());
  // 11. Let offsetAfter be ? GetOffsetNanosecondsFor(timeZone, dayAfter).
  Maybe<int64_t> maybe_offset_after =
      GetOffsetNanosecondsFor(isolate, time_zone, day_after);
  MAYBE_RETURN(maybe_offset_after, Handle<JSTemporalInstant>());

  // 12. Let nanoseconds be offsetAfter − offsetBefore.
  double nanoseconds =
      maybe_offset_after.FromJust() - maybe_offset_before.FromJust();
  // 13. If disambiguation is "earlier", then
  if (disambiguation == Disambiguation::kEarlier) {
    // a. Let earlier be ? AddDateTime(dateTime.[[ISOYear]],
    // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
    // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
    // dateTime.[[ISOMillisecond]],
    // dateTime.[[ISOMicrosecond]], dateTime.[[ISONanosecond]],
    // dateTime.[[Calendar]], 0, 0, 0, 0, 0, 0, 0, 0, 0, −nanoseconds,
    // undefined).
    Maybe<temporal::DateTimeRecordCommon> maybe_earlier = temporal::AddDateTime(
        isolate, date_time->iso_year(), date_time->iso_month(),
        date_time->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
        date_time->iso_second(), date_time->iso_millisecond(),
        date_time->iso_microsecond(), date_time->iso_nanosecond(),
        Handle<JSReceiver>(date_time->calendar(), isolate),
        {0, 0, 0, 0, 0, 0, 0, 0, 0, -nanoseconds},
        isolate->factory()->undefined_value());
    MAYBE_RETURN(maybe_earlier, Handle<JSTemporalInstant>());
    temporal::DateTimeRecordCommon earlier = maybe_earlier.FromJust();

    // See https://github.com/tc39/proposal-temporal/issues/1816
    // b. Let earlierDateTime be ? CreateTemporalDateTime(earlier.[[Year]],
    // earlier.[[Month]], earlier.[[Day]], earlier.[[Hour]], earlier.[[Minute]],
    // earlier.[[Second]], earlier.[[Millisecond]], earlier.[[Microsecond]],
    // earlier.[[Nanosecond]], dateTime.[[Calendar]]).
    Handle<JSTemporalPlainDateTime> earlier_date_time;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, earlier_date_time,
        CreateTemporalDateTime(
            isolate, earlier.year, earlier.month, earlier.day, earlier.hour,
            earlier.minute, earlier.second, earlier.millisecond,
            earlier.microsecond, earlier.nanosecond,
            Handle<JSReceiver>(date_time->calendar(), isolate)),
        JSTemporalInstant);

    // c. Set possibleInstants to ? GetPossibleInstantsFor(timeZone,
    // earlierDateTime).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, possible_instants,
        GetPossibleInstantsFor(isolate, time_zone, earlier_date_time),
        JSTemporalInstant);

    // d. If possibleInstants is empty, throw a RangeError exception.
    if (possible_instants->length() == 0) {
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                      JSTemporalInstant);
    }
    // 3. Return possibleInstants[0].
    Handle<Object> ret_obj = FixedArray::get(*possible_instants, 0, isolate);
    CHECK(ret_obj->IsJSTemporalInstant());
    return Handle<JSTemporalInstant>::cast(ret_obj);
  }
  // 14. Assert: disambiguation is "compatible" or "later".
  CHECK(disambiguation == Disambiguation::kCompatible ||
        disambiguation == Disambiguation::kLater);
  // 15. Let later be ? AddDateTime(dateTime.[[ISOYear]], dateTime.[[ISOMonth]],
  // dateTime.[[ISODay]], dateTime.[[ISOHour]], dateTime.[[ISOMinute]],
  // dateTime.[[ISOSecond]], dateTime.[[ISOMillisecond]],
  // dateTime.[[ISOMicrosecond]], dateTime.[[ISONanosecond]],
  // dateTime.[[Calendar]], 0, 0, 0, 0, 0, 0, 0, 0, 0, nanoseconds, undefined).
  Maybe<temporal::DateTimeRecordCommon> maybe_later = temporal::AddDateTime(
      isolate, date_time->iso_year(), date_time->iso_month(),
      date_time->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
      date_time->iso_second(), date_time->iso_millisecond(),
      date_time->iso_microsecond(), date_time->iso_nanosecond(),
      Handle<JSReceiver>(date_time->calendar(), isolate),
      {0, 0, 0, 0, 0, 0, 0, 0, 0, nanoseconds},
      isolate->factory()->undefined_value());
  MAYBE_RETURN(maybe_later, Handle<JSTemporalInstant>());
  temporal::DateTimeRecordCommon later = maybe_later.FromJust();

  // See https://github.com/tc39/proposal-temporal/issues/1816
  // 16. Let laterDateTime be ? CreateTemporalDateTime(later.[[Year]],
  // later.[[Month]], later.[[Day]], later.[[Hour]], later.[[Minute]],
  // later.[[Second]], later.[[Millisecond]], later.[[Microsecond]],
  // later.[[Nanosecond]], dateTime.[[Calendar]]).

  Handle<JSTemporalPlainDateTime> later_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, later_date_time,
      CreateTemporalDateTime(
          isolate, later.year, later.month, later.day, later.hour, later.minute,
          later.second, later.millisecond, later.microsecond, later.nanosecond,
          Handle<JSReceiver>(date_time->calendar(), isolate)),
      JSTemporalInstant);
  // 17. Set possibleInstants to ? GetPossibleInstantsFor(timeZone,
  // laterDateTime).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, possible_instants,
      GetPossibleInstantsFor(isolate, time_zone, later_date_time),
      JSTemporalInstant);
  // 18. Set n to possibleInstants's length.
  n = possible_instants->length();
  // 19. If n = 0, throw a RangeError exception.
  if (n == 0) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalInstant);
  }
  // 20. Return possibleInstants[n − 1].
  Handle<Object> ret_obj = FixedArray::get(*possible_instants, n - 1, isolate);
  CHECK(ret_obj->IsJSTemporalInstant());
  return Handle<JSTemporalInstant>::cast(ret_obj);
}

// #sec-temporal-builtintimezonegetinstantfor
MaybeHandle<JSTemporalInstant> BuiltinTimeZoneGetInstantFor(
    Isolate* isolate, Handle<JSReceiver> time_zone,
    Handle<JSTemporalPlainDateTime> date_time, Disambiguation disambiguation) {
  TEMPORAL_ENTER_FUNC
  // 1. Assert: dateTime has an [[InitializedTemporalDateTime]] internal slot.
  // 2. Let possibleInstants be ? GetPossibleInstantsFor(timeZone, dateTime).
  Handle<FixedArray> possible_instants;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, possible_instants,
      GetPossibleInstantsFor(isolate, time_zone, date_time), JSTemporalInstant);
  // 3. Return ? DisambiguatePossibleInstants(possibleInstants, timeZone,
  // dateTime, disambiguation).
  return DisambiguatePossibleInstants(isolate, possible_instants, time_zone,
                                      date_time, disambiguation);
}

#define TO_TEMPORAL_WITH_UNDEFINED(T, N)                                 \
  MaybeHandle<JSTemporal##T> ToTemporal##N(                              \
      Isolate* isolate, Handle<Object> item, const char* method) {       \
    /* 1. If options is not present, set options to */                   \
    /* ! OrdinaryObjectCreate(null). */                                  \
    return ToTemporal##N(isolate, item,                                  \
                         isolate->factory()->NewJSObjectWithNullProto(), \
                         method);                                        \
  }

TO_TEMPORAL_WITH_UNDEFINED(PlainDate, Date)
TO_TEMPORAL_WITH_UNDEFINED(PlainDateTime, DateTime)
TO_TEMPORAL_WITH_UNDEFINED(ZonedDateTime, ZonedDateTime)
TO_TEMPORAL_WITH_UNDEFINED(PlainYearMonth, YearMonth)
TO_TEMPORAL_WITH_UNDEFINED(PlainMonthDay, MonthDay)
#undef TO_TEMPORAL_WITH_UNDEFINED

#define IF_IS_TYPE_CAST_RETURN(T, obj)       \
  if (obj->IsJSTemporal##T()) {              \
    return Handle<JSTemporal##T>::cast(obj); \
  }

#define IF_IS_TYPE_CAST_CREATE_DATE(T, obj)                                \
  if (obj->IsJSTemporal##T()) {                                            \
    Handle<JSTemporal##T> t = Handle<JSTemporal##T>::cast(obj);            \
    return CreateTemporalDate(isolate, t->iso_year(), t->iso_month(),      \
                              t->iso_day(),                                \
                              Handle<JSReceiver>(t->calendar(), isolate)); \
  }

#define IF_IS_TYPE_RETURN_CALENDAR(obj, T)                                   \
  if (obj->IsJSTemporal##T())                                                \
    return Handle<JSReceiver>(Handle<JSTemporal##T>::cast(item)->calendar(), \
                              isolate);

namespace {
MaybeHandle<JSReceiver> GetTemporalCalendarWithISODefault(
    Isolate* isolate, Handle<JSReceiver> item, const char* method) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  // 1. If item has an [[InitializedTemporalDate]],
  // [[InitializedTemporalDateTime]], [[InitializedTemporalMonthDay]],
  // [[InitializedTemporalTime]], [[InitializedTemporalYearMonth]], or
  // [[InitializedTemporalZonedDateTime]] internal slot, then a. Return
  // item.[[Calendar]].
  IF_IS_TYPE_RETURN_CALENDAR(item, PlainDate)
  IF_IS_TYPE_RETURN_CALENDAR(item, PlainDateTime)
  IF_IS_TYPE_RETURN_CALENDAR(item, PlainMonthDay)
  IF_IS_TYPE_RETURN_CALENDAR(item, PlainTime)
  IF_IS_TYPE_RETURN_CALENDAR(item, PlainYearMonth)
  IF_IS_TYPE_RETURN_CALENDAR(item, ZonedDateTime)

  // 2. Let calendar be ? Get(item, "calendar").
  Handle<Object> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      JSReceiver::GetProperty(isolate, item, factory->calendar_string()),
      JSReceiver);
  // 3. Return ? ToTemporalCalendarWithISODefault(calendar).
  return ToTemporalCalendarWithISODefault(isolate, calendar, method);
}

}  // namespace

MaybeHandle<JSTemporalPlainDate> ToTemporalDate(Isolate* isolate,
                                                Handle<Object> item_obj,
                                                Handle<JSReceiver> options,
                                                const char* method) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  // 2. Assert: Type(options) is Object.
  // 3. If Type(item) is Object, then
  if (item_obj->IsJSReceiver()) {
    Handle<JSReceiver> item = Handle<JSReceiver>::cast(item_obj);
    // a. If item has an [[InitializedTemporalDate]] internal slot, then
    // i. Return item.
    IF_IS_TYPE_CAST_RETURN(PlainDate, item);
    // b. If item has an [[InitializedTemporalZonedDateTime]] internal slot,
    // then
    if (item->IsJSTemporalZonedDateTime()) {
      // i. Let instant be ! CreateTemporalInstant(item.[[Nanoseconds]]).
      Handle<JSTemporalZonedDateTime> zoned_date_time =
          Handle<JSTemporalZonedDateTime>::cast(item);
      Handle<JSTemporalInstant> instant;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, instant,
          CreateTemporalInstant(
              isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
          JSTemporalPlainDate);
      // ii. Let plainDateTime be ?
      // BuiltinTimeZoneGetPlainDateTimeFor(item.[[TimeZone]], instant,
      // item.[[Calendar]]).
      Handle<JSTemporalPlainDateTime> plain_date_time;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, plain_date_time,
          BuiltinTimeZoneGetPlainDateTimeFor(
              isolate,
              Handle<JSReceiver>(zoned_date_time->time_zone(), isolate),
              instant,
              Handle<JSReceiver>(zoned_date_time->calendar(), isolate)),
          JSTemporalPlainDate);
      // iii. Return ! CreateTemporalDate(plainDateTime.[[ISOYear]],
      // plainDateTime.[[ISOMonth]], plainDateTime.[[ISODay]],
      // plainDateTime.[[Calendar]]).
      return CreateTemporalDate(
          isolate, plain_date_time->iso_year(), plain_date_time->iso_month(),
          plain_date_time->iso_day(),
          Handle<JSReceiver>(plain_date_time->calendar(), isolate));
    }

    // c. If item has an [[InitializedTemporalDateTime]] internal slot, then
    // i. Return ! CreateTemporalDate(item.[[ISOYear]], item.[[ISOMonth]],
    // item.[[ISODay]], item.[[Calendar]]).
    IF_IS_TYPE_CAST_CREATE_DATE(PlainDateTime, item);

    // d. Let calendar be ? GetTemporalCalendarWithISODefault(item).
    Handle<JSReceiver> calendar;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        GetTemporalCalendarWithISODefault(isolate, item, method),
        JSTemporalPlainDate);
    // e. Let fieldNames be ? CalendarFields(calendar, « "day", "month",
    // "monthCode", "year" »).
    Handle<FixedArray> field_names = factory->NewFixedArray(4);
    field_names->set(0, *(factory->day_string()));
    field_names->set(1, *(factory->month_string()));
    field_names->set(2, *(factory->monthCode_string()));
    field_names->set(3, *(factory->year_string()));
    ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                               CalendarFields(isolate, calendar, field_names),
                               JSTemporalPlainDate);
    // f. Let fields be ? PrepareTemporalFields(item,
    // fieldNames, «»).
    Handle<JSReceiver> fields;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, fields,
        PrepareTemporalFields(isolate, item, field_names, false, false),
        JSTemporalPlainDate);
    // g. Return ? DateFromFields(calendar, fields, options).
    return DateFromFields(isolate, calendar, fields, options);
  }
  // 4. Perform ? ToTemporalOverflow(options).
  Maybe<ShowOverflow> maybe_overflow =
      ToTemporalOverflow(isolate, options, method);
  MAYBE_RETURN(maybe_overflow, Handle<JSTemporalPlainDate>());

  // 5. Let string be ? ToString(item).
  Handle<String> string;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                             Object::ToString(isolate, item_obj),
                             JSTemporalPlainDate);
  // 6. Let result be ? ParseTemporalDateString(string).
  Maybe<DateRecord> maybe_result = ParseTemporalDateString(isolate, string);
  MAYBE_RETURN(maybe_result, MaybeHandle<JSTemporalPlainDate>());
  DateRecord result = maybe_result.FromJust();

  // 7. Assert: ! IsValidISODate(result.[[Year]], result.[[Month]],
  // result.[[Day]]) is true.
  CHECK(IsValidISODate(isolate, result.year, result.month, result.day));
  // 8. Let calendar be ? ToTemporalCalendarWithISODefault(result.[[Calendar]]).
  Handle<Object> calendar_string;
  if (result.calendar.empty()) {
    calendar_string = factory->undefined_value();
  } else {
    calendar_string =
        factory->NewStringFromAsciiChecked(result.calendar.c_str());
  }
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ToTemporalCalendarWithISODefault(isolate, calendar_string, method),
      JSTemporalPlainDate);
  // 9. Return ? CreateTemporalDate(result.[[Year]], result.[[Month]],
  // result.[[Day]], calendar).
  return CreateTemporalDate(isolate, result.year, result.month, result.day,
                            calendar);
}

// #sec-temporal-totemporaldatetime
MaybeHandle<JSTemporalPlainDateTime> ToTemporalDateTime(
    Isolate* isolate, Handle<Object> item_obj, Handle<JSReceiver> options,
    const char* method) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  Handle<JSReceiver> calendar;
  DateTimeRecord result;
  // 2. If Type(item) is Object, then
  if (item_obj->IsJSReceiver()) {
    Handle<JSReceiver> item = Handle<JSReceiver>::cast(item_obj);
    // a. If item has an [[InitializedTemporalDateTime]] internal slot, then
    // i. Return item.
    IF_IS_TYPE_CAST_RETURN(PlainDateTime, item);
    // b. If item has an [[InitializedTemporalZonedDateTime]] internal slot,
    // then
    if (item->IsJSTemporalZonedDateTime()) {
      // i. Let instant be ! CreateTemporalInstant(item.[[Nanoseconds]]).
      Handle<JSTemporalZonedDateTime> zoned_date_time =
          Handle<JSTemporalZonedDateTime>::cast(item);
      Handle<JSTemporalInstant> instant;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, instant,
          CreateTemporalInstant(
              isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
          JSTemporalPlainDateTime);
      // ii. Return ? BuiltinTimeZoneGetPlainDateTimeFor(item.[[TimeZone]],
      // instant, item.[[Calendar]]).
      return BuiltinTimeZoneGetPlainDateTimeFor(
          isolate, Handle<JSReceiver>(zoned_date_time->time_zone(), isolate),
          instant, Handle<JSReceiver>(zoned_date_time->calendar(), isolate));
    }
    // c. If item has an [[InitializedTemporalDate]] internal slot, then
    if (item->IsJSTemporalPlainDate()) {
      // i. Return ? CreateTemporalDateTime(item.[[ISOYear]], item.[[ISOMonth]],
      // item.[[ISODay]], 0, 0, 0, 0, 0, 0, item.[[Calendar]]).
      Handle<JSTemporalPlainDate> date =
          Handle<JSTemporalPlainDate>::cast(item);
      return CreateTemporalDateTime(
          isolate, date->iso_year(), date->iso_month(), date->iso_day(), 0, 0,
          0, 0, 0, 0, Handle<JSReceiver>(date->calendar(), isolate));
    }
    // d. Let calendar be ? GetTemporalCalendarWithISODefault(item).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        GetTemporalCalendarWithISODefault(isolate, item, method),
        JSTemporalPlainDateTime);
    // e. Let fieldNames be ? CalendarFields(calendar, « "day", "hour",
    // "microsecond", "millisecond", "minute", "month", "monthCode",
    // "nanosecond", "second", "year" »).
    Handle<FixedArray> field_names = factory->NewFixedArray(10);
    field_names->set(0, *(factory->day_string()));
    field_names->set(1, *(factory->hour_string()));
    field_names->set(2, *(factory->microsecond_string()));
    field_names->set(3, *(factory->millisecond_string()));
    field_names->set(4, *(factory->minute_string()));
    field_names->set(5, *(factory->month_string()));
    field_names->set(6, *(factory->monthCode_string()));
    field_names->set(7, *(factory->nanosecond_string()));
    field_names->set(8, *(factory->second_string()));
    field_names->set(9, *(factory->year_string()));
    ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                               CalendarFields(isolate, calendar, field_names),
                               JSTemporalPlainDateTime);
    // f. Let fields be ? PrepareTemporalFields(item,
    // PrepareTemporalFields(item, fieldNames, «»).
    Handle<JSReceiver> fields;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, fields,
        PrepareTemporalFields(isolate, item, field_names, false, false),
        JSTemporalPlainDateTime);
    // g. Let result be ?
    // InterpretTemporalDateTimeFields(calendar, fields, options).
    Maybe<DateTimeRecord> maybe_result = InterpretTemporalDateTimeFields(
        isolate, calendar, fields, options, method);
    MAYBE_RETURN(maybe_result, Handle<JSTemporalPlainDateTime>());
    result = maybe_result.FromJust();
  } else {
    // 3. Else,
    // a. Perform ? ToTemporalOverflow(options).
    Maybe<ShowOverflow> maybe_overflow =
        ToTemporalOverflow(isolate, options, method);
    MAYBE_RETURN(maybe_overflow, Handle<JSTemporalPlainDateTime>());

    // b. Let string be ? ToString(item).
    Handle<String> string;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                               Object::ToString(isolate, item_obj),
                               JSTemporalPlainDateTime);
    // c. Let result be ? ParseTemporalDateTimeString(string).
    Maybe<DateTimeRecord> maybe_result =
        ParseTemporalDateTimeString(isolate, string);
    MAYBE_RETURN(maybe_result, MaybeHandle<JSTemporalPlainDateTime>());
    result = maybe_result.FromJust();
    // d. Assert: ! IsValidISODate(result.[[Year]], result.[[Month]],
    // result.[[Day]]) is true.
    CHECK(IsValidISODate(isolate, result.year, result.month, result.day));
    // e. Assert: ! IsValidTime(result.[[Hour]],
    // result.[[Minute]], result.[[Second]], result.[[Millisecond]],
    // result.[[Microsecond]], result.[[Nanosecond]]) is true.
    CHECK(IsValidTime(isolate, result.hour, result.minute, result.second,
                      result.millisecond, result.microsecond,
                      result.nanosecond));
    // f. Let calendar
    // be ? ToTemporalCalendarWithISODefault(result.[[Calendar]]).
    Handle<Object> calendar_string;
    if (result.calendar.empty()) {
      calendar_string = factory->undefined_value();
    } else {
      calendar_string =
          factory->NewStringFromAsciiChecked(result.calendar.c_str());
    }
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        ToTemporalCalendarWithISODefault(isolate, calendar_string, method),
        JSTemporalPlainDateTime);
  }
  // 4. Return ? CreateTemporalDateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // calendar).
  return CreateTemporalDateTime(isolate, result.year, result.month, result.day,
                                result.hour, result.minute, result.second,
                                result.millisecond, result.microsecond,
                                result.nanosecond, calendar);
}

// #sec-temporal-totemporaltime
MaybeHandle<JSTemporalPlainTime> ToTemporalTime(Isolate* isolate,
                                                Handle<Object> item_obj,
                                                const char* method) {
  TEMPORAL_ENTER_FUNC

  // 1. If overflow is not present, set it to "constrain".
  return ToTemporalTime(isolate, item_obj, ShowOverflow::kConstrain, method);
}
MaybeHandle<JSTemporalPlainTime> ToTemporalTime(Isolate* isolate,
                                                Handle<Object> item_obj,
                                                ShowOverflow overflow,
                                                const char* method) {
  Factory* factory = isolate->factory();
  TimeRecord result;
  // 2. Assert: overflow is either "constrain" or "reject".
  // 3. If Type(item) is Object, then
  if (item_obj->IsJSReceiver()) {
    Handle<JSReceiver> item = Handle<JSReceiver>::cast(item_obj);
    // a. If item has an [[InitializedTemporalTime]] internal slot, then
    // i. Return item.
    IF_IS_TYPE_CAST_RETURN(PlainTime, item);
    // b. If item has an [[InitializedTemporalZonedDateTime]] internal slot,
    // then
    if (item->IsJSTemporalZonedDateTime()) {
      // i. Let instant be ! CreateTemporalInstant(item.[[Nanoseconds]]).
      Handle<JSTemporalZonedDateTime> zoned_date_time =
          Handle<JSTemporalZonedDateTime>::cast(item);
      Handle<JSTemporalInstant> instant;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, instant,
          CreateTemporalInstant(
              isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
          JSTemporalPlainTime);
      // ii. Set plainDateTime to ?
      // BuiltinTimeZoneGetPlainDateTimeFor(item.[[TimeZone]], instant,
      // item.[[Calendar]]).
      Handle<JSTemporalPlainDateTime> plain_date_time;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, plain_date_time,
          BuiltinTimeZoneGetPlainDateTimeFor(
              isolate,
              Handle<JSReceiver>(zoned_date_time->time_zone(), isolate),
              instant,
              Handle<JSReceiver>(zoned_date_time->calendar(), isolate)),
          JSTemporalPlainTime);
      // iii. Return !
      // CreateTemporalTime(plainDateTime.[[ISOHour]],
      // plainDateTime.[[ISOMinute]], plainDateTime.[[ISOSecond]],
      // plainDateTime.[[ISOMillisecond]], plainDateTime.[[ISOMicrosecond]],
      // plainDateTime.[[ISONanosecond]]).
      return CreateTemporalTime(
          isolate, plain_date_time->iso_hour(), plain_date_time->iso_minute(),
          plain_date_time->iso_second(), plain_date_time->iso_millisecond(),
          plain_date_time->iso_microsecond(),
          plain_date_time->iso_nanosecond());
    }
    // c. If item has an [[InitializedTemporalDateTime]] internal slot, then
    if (item->IsJSTemporalPlainDateTime()) {
      // i. Return ! CreateTemporalTime(item.[[ISOHour]], item.[[ISOMinute]],
      // item.[[ISOSecond]], item.[[ISOMillisecond]], item.[[ISOMicrosecond]],
      // item.[[ISONanosecond]]).
      Handle<JSTemporalPlainDateTime> date_time =
          Handle<JSTemporalPlainDateTime>::cast(item);
      return CreateTemporalTime(
          isolate, date_time->iso_hour(), date_time->iso_minute(),
          date_time->iso_second(), date_time->iso_millisecond(),
          date_time->iso_microsecond(), date_time->iso_nanosecond());
    }
    // d. Let calendar be ? GetTemporalCalendarWithISODefault(item).
    Handle<JSReceiver> calendar;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        GetTemporalCalendarWithISODefault(isolate, item, method),
        JSTemporalPlainTime);
    // e. If ? ToString(calendar) is not "iso8601", then
    Handle<String> identifier;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, identifier,
                               Object::ToString(isolate, calendar),
                               JSTemporalPlainTime);
    if (!String::Equals(isolate, factory->iso8601_string(), identifier)) {
      // i. Throw a RangeError exception.
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                      JSTemporalPlainTime);
    }
    // f. Let result be ? ToTemporalTimeRecord(item).
    Maybe<TimeRecord> maybe_time_result =
        ToTemporalTimeRecord(isolate, item, method);
    MAYBE_RETURN(maybe_time_result, Handle<JSTemporalPlainTime>());
    result = maybe_time_result.FromJust();
    // g. Set result to ? RegulateTime(result.[[Hour]], result.[[Minute]],
    // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
    // result.[[Nanosecond]], overflow).
    Maybe<bool> maybe_regulate_time = RegulateTime(
        isolate, &result.hour, &result.minute, &result.second,
        &result.millisecond, &result.microsecond, &result.nanosecond, overflow);
    MAYBE_RETURN(maybe_regulate_time, Handle<JSTemporalPlainTime>());
  } else {
    // 4. Else,
    // a. Let string be ? ToString(item).
    Handle<String> string;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                               Object::ToString(isolate, item_obj),
                               JSTemporalPlainTime);
    // b. Let result be ? ParseTemporalTimeString(string).
    Maybe<TimeRecord> maybe_result = ParseTemporalTimeString(isolate, string);
    MAYBE_RETURN(maybe_result, MaybeHandle<JSTemporalPlainTime>());
    result = maybe_result.FromJust();
    // c. Assert: ! IsValidTime(result.[[Hour]], result.[[Minute]],
    // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
    // result.[[Nanosecond]]) is true.
    CHECK(IsValidTime(isolate, result.hour, result.minute, result.second,
                      result.millisecond, result.microsecond,
                      result.nanosecond));
    // d. If result.[[Calendar]] is not one of undefined or "iso8601", then
    if ((!result.calendar.empty()) && result.calendar != "iso8601") {
      // i. Throw a RangeError exception.
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                      JSTemporalPlainTime);
    }
  }
  // 5. Return ? CreateTemporalTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]).
  return CreateTemporalTime(isolate, result.hour, result.minute, result.second,
                            result.millisecond, result.microsecond,
                            result.nanosecond);
}

// #sec-temporal-totemporalmonthday
MaybeHandle<JSTemporalPlainMonthDay> ToTemporalMonthDay(
    Isolate* isolate, Handle<Object> item_obj, Handle<JSReceiver> options,
    const char* method) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  // 1. If options is not present, set options to ! OrdinaryObjectCreate(null).
  // 2. Let referenceISOYear be 1972 (the first leap year after the Unix epoch).
  int32_t reference_iso_year = 1972;
  // 3. If Type(item) is Object, then
  if (item_obj->IsJSReceiver()) {
    Handle<JSReceiver> item = Handle<JSReceiver>::cast(item_obj);
    // a. If item has an [[InitializedTemporalMonthDay]] internal slot, then
    // i. Return item.
    IF_IS_TYPE_CAST_RETURN(PlainMonthDay, item_obj)
    // b. If item has an [[InitializedTemporalDate]],
    // [[InitializedTemporalDateTime]], [[InitializedTemporalTime]],
    // [[InitializedTemporalYearMonth]], or [[InitializedTemporalZonedDateTime]]
    // internal slot, then
    Handle<JSReceiver> calendar;
    bool calendar_absent = true;
#define EXTRACT_CALENDAR(T, obj)                                \
  if (obj->IsJSTemporal##T()) {                                 \
    /* i. Let calendar be item.[[Calendar]]. */                 \
    calendar = Handle<JSReceiver>(                              \
        Handle<JSTemporal##T>::cast(obj)->calendar(), isolate); \
    /* ii. Let calendarAbsent be false. */                      \
    calendar_absent = false;                                    \
  }

    EXTRACT_CALENDAR(PlainDate, item_obj)
    EXTRACT_CALENDAR(PlainDateTime, item_obj)
    EXTRACT_CALENDAR(PlainTime, item_obj)
    EXTRACT_CALENDAR(PlainYearMonth, item_obj)
    EXTRACT_CALENDAR(ZonedDateTime, item_obj)
#undef EXTRACT_CALENDAR
    // c. Else,
    if (calendar_absent) {
      // i. Let calendar be ? Get(item, "calendar").
      Handle<Object> calendar_obj;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, calendar_obj,
          JSReceiver::GetProperty(isolate, item, factory->calendar_string()),
          JSTemporalPlainMonthDay);
      // ii. If calendar is undefined, then
      if (calendar_obj->IsUndefined()) {
        // 1. Let calendarAbsent be true.
        calendar_absent = true;
      } else {
        // iii. Else,
        // 1. Let calendarAbsent be false.
        calendar_absent = false;
      }
      // iv. Set calendar to ? ToTemporalCalendarWithISODefault(calendar).
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, calendar,
          ToTemporalCalendarWithISODefault(isolate, calendar_obj, method),
          JSTemporalPlainMonthDay);
    }
    // d. Let fieldNames be ? CalendarFields(calendar, « "day", "month",
    // "monthCode", "year" »).
    Handle<FixedArray> field_names = factory->NewFixedArray(4);
    int i = 0;
    field_names->set(i++, *(factory->day_string()));
    field_names->set(i++, *(factory->month_string()));
    field_names->set(i++, *(factory->monthCode_string()));
    field_names->set(i++, *(factory->year_string()));
    CHECK_EQ(i, 4);
    ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                               CalendarFields(isolate, calendar, field_names),
                               JSTemporalPlainMonthDay);
    // e. Let fields be ? PrepareTemporalFields(item, fieldNames, «»).
    Handle<JSObject> fields;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, fields,
        PrepareTemporalFields(isolate, item, field_names, false, false),
        JSTemporalPlainMonthDay);
    // f. Let month be ? Get(fields, "month").
    Handle<Object> month;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, month,
        JSReceiver::GetProperty(isolate, fields, factory->month_string()),
        JSTemporalPlainMonthDay);
    // g. Let monthCode be ? Get(fields, "monthCode").
    Handle<Object> month_code;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, month_code,
        JSReceiver::GetProperty(isolate, fields, factory->monthCode_string()),
        JSTemporalPlainMonthDay);
    // h. Let year be ? Get(fields, "year").
    Handle<Object> year;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, year,
        JSReceiver::GetProperty(isolate, fields, factory->year_string()),
        JSTemporalPlainMonthDay);
    // i. If calendarAbsent is true, and month is not undefined, and monthCode
    // is undefined and year is undefined, then
    if (calendar_absent && (!month->IsUndefined()) &&
        month_code->IsUndefined() && year->IsUndefined()) {
      // i. Perform ! CreateDataPropertyOrThrow(fields, "year",
      // 𝔽(referenceISOYear)).
      CHECK(JSReceiver::CreateDataProperty(
                isolate, fields, factory->year_string(),
                Handle<Smi>(Smi::FromInt(reference_iso_year), isolate),
                Just(kThrowOnError))
                .FromJust());
    }
    // j. Return ? MonthDayFromFields(calendar, fields, options).
    return MonthDayFromFields(isolate, calendar, fields, options);
  }
  // 4. Perform ? ToTemporalOverflow(options).
  Maybe<ShowOverflow> maybe_overflow =
      ToTemporalOverflow(isolate, options, method);
  MAYBE_RETURN(maybe_overflow, Handle<JSTemporalPlainMonthDay>());

  // 5. Let string be ? ToString(item).
  Handle<String> string;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                             Object::ToString(isolate, item_obj),
                             JSTemporalPlainMonthDay);

  // 6. Let result be ? ParseTemporalMonthDayString(string).
  Maybe<DateRecord> maybe_result = ParseTemporalMonthDayString(isolate, string);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalPlainMonthDay>());
  DateRecord result = maybe_result.FromJust();

  // 7. Let calendar be ? ToTemporalCalendarWithISODefault(result.[[Calendar]]).
  Handle<Object> calendar_string;
  if (result.calendar.empty()) {
    calendar_string = factory->undefined_value();
  } else {
    calendar_string =
        factory->NewStringFromAsciiChecked(result.calendar.c_str());
  }
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ToTemporalCalendarWithISODefault(isolate, calendar_string, method),
      JSTemporalPlainMonthDay);

  // 8. If result.[[Year]] is undefined, then
  // We use kMintInt31 to represent undefined
  if (result.year == kMinInt31) {
    // a. Return ? CreateTemporalMonthDay(result.[[Month]], result.[[Day]],
    // calendar, referenceISOYear).
    return CreateTemporalMonthDay(isolate, result.month, result.day, calendar,
                                  reference_iso_year);
  }

  Handle<JSTemporalPlainMonthDay> created_result;
  // 9. Set result to ? CreateTemporalMonthDay(result.[[Month]], result.[[Day]],
  // calendar, referenceISOYear).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, created_result,
      CreateTemporalMonthDay(isolate, result.month, result.day, calendar,
                             reference_iso_year),
      JSTemporalPlainMonthDay);
  // 10. Let canonicalMonthDayOptions be ! OrdinaryObjectCreate(null).
  Handle<JSReceiver> canonical_month_day_options =
      factory->NewJSObjectWithNullProto();

  // 11. Return ? MonthDayFromFields(calendar, result,
  // canonicalMonthDayOptions).
  return MonthDayFromFields(isolate, calendar, created_result,
                            canonical_month_day_options);
}

// #sec-temporal-totemporalyearmonth
MaybeHandle<JSTemporalPlainYearMonth> ToTemporalYearMonth(
    Isolate* isolate, Handle<Object> item_obj, Handle<JSReceiver> options,
    const char* method) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  // 1. If options is not present, set options to ! OrdinaryObjectCreate(null).
  // 2. Assert: Type(options) is Object.
  // 3. If Type(item) is Object, then
  if (item_obj->IsJSReceiver()) {
    Handle<JSReceiver> item = Handle<JSReceiver>::cast(item_obj);
    // a. If item has an [[InitializedTemporalYearMonth]] internal slot, then
    // i. Return item.
    IF_IS_TYPE_CAST_RETURN(PlainYearMonth, item_obj)
    // b. Let calendar be ? GetTemporalCalendarWithISODefault(item).
    Handle<JSReceiver> calendar;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        GetTemporalCalendarWithISODefault(isolate, item, method),
        JSTemporalPlainYearMonth);
    // c. Let fieldNames be ? CalendarFields(calendar, « "month", "monthCode",
    // "year" »).
    Handle<FixedArray> field_names = factory->NewFixedArray(3);
    field_names->set(0, *(factory->month_string()));
    field_names->set(1, *(factory->monthCode_string()));
    field_names->set(2, *(factory->year_string()));
    ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                               CalendarFields(isolate, calendar, field_names),
                               JSTemporalPlainYearMonth);
    // d. Let fields be ? PrepareTemporalFields(item, fieldNames, «»).
    Handle<JSReceiver> fields;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, fields,
        PrepareTemporalFields(isolate, item, field_names, false, false),
        JSTemporalPlainYearMonth);
    // e. Return ? YearMonthFromFields(calendar, fields, options).
    return YearMonthFromFields(isolate, calendar, fields, options);
  }
  // 4. Perform ? ToTemporalOverflow(options).
  Maybe<ShowOverflow> maybe_overflow =
      ToTemporalOverflow(isolate, options, method);
  MAYBE_RETURN(maybe_overflow, Handle<JSTemporalPlainYearMonth>());
  // 5. Let string be ? ToString(item).
  Handle<String> string;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                             Object::ToString(isolate, item_obj),
                             JSTemporalPlainYearMonth);
  // 6. Let result be ? ParseTemporalYearMonthString(string).
  Maybe<DateRecord> maybe_result =
      ParseTemporalYearMonthString(isolate, string);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalPlainYearMonth>());
  DateRecord result = maybe_result.FromJust();
  // 7. Let calendar be ? ToTemporalCalendarWithISODefault(result.[[Calendar]]).
  Handle<Object> calendar_string;
  if (result.calendar.empty()) {
    calendar_string = factory->undefined_value();
  } else {
    calendar_string =
        factory->NewStringFromAsciiChecked(result.calendar.c_str());
  }
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      ToTemporalCalendarWithISODefault(isolate, calendar_string, method),
      JSTemporalPlainYearMonth);
  // 8. Set result to ? CreateTemporalYearMonth(result.[[Year]],
  // result.[[Month]], calendar, result.[[Day]]).
  Handle<JSTemporalPlainYearMonth> created_result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, created_result,
      CreateTemporalYearMonth(isolate, result.year, result.month, calendar,
                              result.day),
      JSTemporalPlainYearMonth);
  // 9. Let canonicalYearMonthOptions be ! OrdinaryObjectCreate(null).
  Handle<JSReceiver> canonical_year_month_options =
      factory->NewJSObjectWithNullProto();
  // 10. Return ? YearMonthFromFields(calendar, result,
  // canonicalYearMonthOptions).
  return YearMonthFromFields(isolate, calendar, created_result,
                             canonical_year_month_options);
}

// #sec-temporal-totemporalzoneddatetime
MaybeHandle<JSTemporalZonedDateTime> ToTemporalZonedDateTime(
    Isolate* isolate, Handle<Object> item_obj, Handle<JSReceiver> options,
    const char* method) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  Handle<String> offset_string;
  Handle<JSReceiver> time_zone;
  DateTimeRecord result1;
  ZonedDateTimeRecord result2;
  bool from_result2 = false;
  Handle<JSReceiver> calendar;
  // 1. If options is not present, set options to ! OrdinaryObjectCreate(null).
  // 2. Let offsetBehaviour be option.
  OffsetBehaviour offset_behaviour = OffsetBehaviour::kOption;
  //
  // 3. If Type(item) is Object, then
  if (item_obj->IsJSReceiver()) {
    Handle<JSReceiver> item = Handle<JSReceiver>::cast(item_obj);
    // a. If item has an [[InitializedTemporalZonedDateTime]] internal slot,
    // then i. Return item.
    IF_IS_TYPE_CAST_RETURN(ZonedDateTime, item);
    // b. Let calendar be ? GetTemporalCalendarWithISODefault(item).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        GetTemporalCalendarWithISODefault(isolate, item, method),
        JSTemporalZonedDateTime);
    // c. Let fieldNames be ? CalendarFields(calendar, « "day", "hour",
    // "microsecond", "millisecond", "minute", "month", "monthCode",
    // "nanosecond", "second", "year" »).
    Handle<FixedArray> field_names = factory->NewFixedArray(10);
    field_names->set(0, *(factory->day_string()));
    field_names->set(1, *(factory->hour_string()));
    field_names->set(2, *(factory->microsecond_string()));
    field_names->set(3, *(factory->millisecond_string()));
    field_names->set(4, *(factory->minute_string()));
    field_names->set(5, *(factory->month_string()));
    field_names->set(6, *(factory->monthCode_string()));
    field_names->set(7, *(factory->nanosecond_string()));
    field_names->set(8, *(factory->second_string()));
    field_names->set(9, *(factory->year_string()));
    ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                               CalendarFields(isolate, calendar, field_names),
                               JSTemporalZonedDateTime);
    // d. Append "timeZone" to fieldNames.
    field_names =
        FixedArray::SetAndGrow(isolate, field_names, field_names->length(),
                               factory->timeZone_string());

    // e. Let fields be ? PrepareTemporalFields(item, fieldNames, « "timeZone"
    // »).
    Handle<JSReceiver> fields;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, fields,
        PrepareTemporalFields(isolate, item, field_names, false, true),
        JSTemporalZonedDateTime);
    // f. Let timeZone be ? Get(fields, "timeZone").
    Handle<Object> time_zone_obj;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone_obj,
        JSReceiver::GetProperty(isolate, fields, factory->timeZone_string()),
        JSTemporalZonedDateTime);
    // g. Set timeZone to ? ToTemporalTimeZone(timeZone).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone, ToTemporalTimeZone(isolate, time_zone_obj, method),
        JSTemporalZonedDateTime);
    // h. Let offsetString be ? Get(fields, "offset").
    Handle<Object> offset_string_obj;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, offset_string_obj,
        JSReceiver::GetProperty(isolate, fields, factory->offset_string()),
        JSTemporalZonedDateTime);
    // i. If offsetString is undefined, then
    if (offset_string_obj->IsUndefined()) {
      // i. Set offsetBehaviour to wall.
      offset_behaviour = OffsetBehaviour::kWall;
      // j. Else,
    } else {
      // i. Set offsetString to ? ToString(offsetString).
      ASSIGN_RETURN_ON_EXCEPTION(isolate, offset_string,
                                 Object::ToString(isolate, offset_string_obj),
                                 JSTemporalZonedDateTime);
    }

    // j. Let result be ? InterpretTemporalDateTimeFields(calendar, fields,
    // options).
    Maybe<DateTimeRecord> maybe_result = InterpretTemporalDateTimeFields(
        isolate, calendar, fields, options, method);
    MAYBE_RETURN(maybe_result, Handle<JSTemporalZonedDateTime>());
    from_result2 = false;
    result1 = maybe_result.FromJust();
    // 4. Else,
  } else {
    // a. Perform ? ToTemporalOverflow(options).
    Maybe<ShowOverflow> maybe_overflow =
        ToTemporalOverflow(isolate, options, method);
    MAYBE_RETURN(maybe_overflow, Handle<JSTemporalZonedDateTime>());
    // b. Let string be ? ToString(item).
    Handle<String> string;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                               Object::ToString(isolate, item_obj),
                               JSTemporalZonedDateTime);
    // c. Let result be ? ParseTemporalZonedDateTimeString(string).
    Maybe<ZonedDateTimeRecord> maybe_result =
        ParseTemporalZonedDateTimeString(isolate, string);
    MAYBE_RETURN(maybe_result, Handle<JSTemporalZonedDateTime>());
    from_result2 = true;
    result2 = maybe_result.FromJust();
    // d. Assert: result.[[TimeZoneName]] is not undefined.
    CHECK(!result2.time_zone_name.empty());

    // e. Let offsetString be result.[[TimeZoneOffsetString]].
    offset_string =
        factory->NewStringFromAsciiChecked(result2.offset_string.c_str());

    // f. If result.[[TimeZoneZ]] is true, then
    if (result2.time_zone_z) {
      // i. Set offsetBehaviour to exact.
      offset_behaviour = OffsetBehaviour::kExact;

      // g. Else if offsetString is undefined, then
    }
    if (offset_string->IsUndefined()) {
      // i. Set offsetBehaviour to wall.
      offset_behaviour = OffsetBehaviour::kWall;
    }

    // h. Let timeZone be ? CreateTemporalTimeZone(result.[[TimeZoneName]]).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone,
        CreateTemporalTimeZone(isolate, result2.time_zone_name),
        JSTemporalZonedDateTime);

    Handle<Object> calendar_string;
    if (result2.calendar.empty()) {
      calendar_string = factory->undefined_value();
    } else {
      calendar_string =
          factory->NewStringFromAsciiChecked(result2.calendar.c_str());
    }

    // i. Let calendar be ?
    // ToTemporalCalendarWithISODefault(result.[[Calendar]]).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        ToTemporalCalendarWithISODefault(isolate, calendar_string, method),
        JSTemporalZonedDateTime);
  }
  // 5. Let offsetNanoseconds be 0.
  int64_t offset_nanoseconds = 0;

  // 6. If offsetBehaviour is option, then
  if (offset_behaviour == OffsetBehaviour::kOption) {
    // a. Set offsetNanoseconds to ? ParseTimeZoneOffsetString(offsetString).
    Maybe<int64_t> maybe_offset_nanoseconds =
        ParseTimeZoneOffsetString(isolate, offset_string);
    MAYBE_RETURN(maybe_offset_nanoseconds, Handle<JSTemporalZonedDateTime>());
    offset_nanoseconds = maybe_offset_nanoseconds.FromJust();
  }

  // 7. Let disambiguation be ? ToTemporalDisambiguation(options).
  Maybe<Disambiguation> maybe_disambiguation =
      ToTemporalDisambiguation(isolate, options, method);
  MAYBE_RETURN(maybe_disambiguation, Handle<JSTemporalZonedDateTime>());
  Disambiguation disambiguation = maybe_disambiguation.FromJust();

  // 8. Let offset be ? ToTemporalOffset(options, "reject").
  Maybe<Offset> maybe_offset =
      ToTemporalOffset(isolate, options, Offset::kReject, method);
  MAYBE_RETURN(maybe_offset, Handle<JSTemporalZonedDateTime>());
  Offset offset = maybe_offset.FromJust();

  // 9. Let epochNanoseconds be ? InterpretISODateTimeOffset(result.[[Year]],
  // result.[[Month]], result.[[Day]], result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]], offsetBehaviour, offsetNanoseconds, timeZone,
  // disambiguation, offset).
  //
  Handle<BigInt> epoch_nanoseconds;
  if (from_result2) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, epoch_nanoseconds,
        InterpretISODateTimeOffset(
            isolate, result2.year, result2.month, result2.day, result2.hour,
            result2.minute, result2.second, result2.millisecond,
            result2.microsecond, result2.nanosecond, offset_behaviour,
            offset_nanoseconds, time_zone, disambiguation, offset, method),
        JSTemporalZonedDateTime);
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, epoch_nanoseconds,
        InterpretISODateTimeOffset(
            isolate, result1.year, result1.month, result1.day, result1.hour,
            result1.minute, result1.second, result1.millisecond,
            result1.microsecond, result1.nanosecond, offset_behaviour,
            offset_nanoseconds, time_zone, disambiguation, offset, method),
        JSTemporalZonedDateTime);
  }

  // 8. Return ? CreateTemporalZonedDateTime(epochNanoseconds, timeZone,
  // calendar).
  return CreateTemporalZonedDateTime(isolate, epoch_nanoseconds, time_zone,
                                     calendar);
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

// #sec-temporal-createtemporalcalendar
MaybeHandle<JSTemporalInstant> ToTemporalInstant(Isolate* isolate,
                                                 Handle<Object> item,
                                                 const char* method) {
  TEMPORAL_ENTER_FUNC

  // 1. If Type(item) is Object, then
  // a. If item has an [[InitializedTemporalInstant]] internal slot, then
  if (item->IsJSTemporalInstant()) {
    // i. Return item.
    Handle<JSTemporalInstant> instant = Handle<JSTemporalInstant>::cast(item);
    return instant;
  }
  // b. If item has an [[InitializedTemporalZonedDateTime]] internal slot, then
  if (item->IsJSTemporalZonedDateTime()) {
    // i. Return ! CreateTemporalInstant(item.[[Nanoseconds]]).
    Handle<BigInt> nanoseconds = Handle<BigInt>(
        JSTemporalZonedDateTime::cast(*item).nanoseconds(), isolate);
    return CreateTemporalInstant(isolate, nanoseconds);
  }
  // 2. Let string be ? ToString(item).
  Handle<String> string;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, string, Object::ToString(isolate, item),
                             JSTemporalInstant);

  // 3. Let epochNanoseconds be ? ParseTemporalInstant(string).
  Handle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, epoch_nanoseconds,
                             ParseTemporalInstant(isolate, string),
                             JSTemporalInstant);

  // 4. Return ? CreateTemporalInstant(ℤ(epochNanoseconds)).
  return CreateTemporalInstant(isolate, epoch_nanoseconds);
}

// #sec-temporal-totemporalduration
MaybeHandle<JSTemporalDuration> ToTemporalDuration(Isolate* isolate,
                                                   Handle<Object> item,
                                                   const char* method) {
  TEMPORAL_ENTER_FUNC

  DurationRecord result;
  // 1. If Type(item) is Object, then
  if (item->IsJSReceiver()) {
    // a. If item has an [[InitializedTemporalDuration]] internal slot, then
    IF_IS_TYPE_CAST_RETURN(Duration, item);

    // b. Let result be ? ToTemporalDurationRecord(item).
    Maybe<DurationRecord> maybe_result = ToTemporalDurationRecord(
        isolate, Handle<JSReceiver>::cast(item), method);
    MAYBE_RETURN(maybe_result, Handle<JSTemporalDuration>());
    result = maybe_result.FromJust();
  } else {
    // 2. Else,
    // a. Let string be ? ToString(item).
    Handle<String> string;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, string, Object::ToString(isolate, item),
                               JSTemporalDuration);
    // b. Let result be ? ParseTemporalDurationString(string).
    Maybe<DurationRecord> maybe_result =
        ParseTemporalDurationString(isolate, string);
    MAYBE_RETURN(maybe_result, Handle<JSTemporalDuration>());
    result = maybe_result.FromJust();
  }
  // 3. Return ? CreateTemporalDuration(result.[[Years]], result.[[Months]],
  // result.[[Weeks]], result.[[Days]], result.[[Hours]], result.[[Minutes]],
  // result.[[Seconds]], result.[[Milliseconds]], result.[[Microseconds]],
  // result.[[Nanoseconds]]).
  return CreateTemporalDuration(
      isolate, result.years, result.months, result.weeks, result.days,
      result.hours, result.minutes, result.seconds, result.milliseconds,
      result.microseconds, result.nanoseconds);
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

// #sec-temporal-systemdatetime
MaybeHandle<JSTemporalPlainDateTime> SystemDateTime(
    Isolate* isolate, Handle<Object> temporal_time_zone_like,
    Handle<Object> calendar_like, const char* method) {
  TEMPORAL_ENTER_FUNC

  Handle<JSReceiver> time_zone;
  // 1. 1. If temporalTimeZoneLike is undefined, then
  if (temporal_time_zone_like->IsUndefined()) {
    // a. Let timeZone be ! SystemTimeZone().
    ASSIGN_RETURN_ON_EXCEPTION(isolate, time_zone, SystemTimeZone(isolate),
                               JSTemporalPlainDateTime);
  } else {
    // 2. Else,
    // a. Let timeZone be ? ToTemporalTimeZone(temporalTimeZoneLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone,
        ToTemporalTimeZone(isolate, temporal_time_zone_like, method),
        JSTemporalPlainDateTime);
  }
  Handle<JSReceiver> calendar;
  // 3. Let calendar be ? ToTemporalCalendar(calendarLike).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             ToTemporalCalendar(isolate, calendar_like, method),
                             JSTemporalPlainDateTime);
  // 4. Let instant be ! SystemInstant().
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, instant, SystemInstant(isolate),
                             JSTemporalPlainDateTime);
  // 5. Return ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant,
  // calendar).
  return BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, instant,
                                            calendar);
}

MaybeHandle<JSTemporalZonedDateTime> SystemZonedDateTime(
    Isolate* isolate, Handle<Object> temporal_time_zone_like,
    Handle<Object> calendar_like, const char* method) {
  TEMPORAL_ENTER_FUNC

  Handle<JSReceiver> time_zone;
  // 1. 1. If temporalTimeZoneLike is undefined, then
  if (temporal_time_zone_like->IsUndefined()) {
    // a. Let timeZone be ! SystemTimeZone().
    ASSIGN_RETURN_ON_EXCEPTION(isolate, time_zone, SystemTimeZone(isolate),
                               JSTemporalZonedDateTime);
  } else {
    // 2. Else,
    // a. Let timeZone be ? ToTemporalTimeZone(temporalTimeZoneLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone,
        ToTemporalTimeZone(isolate, temporal_time_zone_like, method),
        JSTemporalZonedDateTime);
  }
  Handle<JSReceiver> calendar;
  // 3. Let calendar be ? ToTemporalCalendar(calendarLike).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             ToTemporalCalendar(isolate, calendar_like, method),
                             JSTemporalZonedDateTime);
  // 4. Let ns be ! SystemUTCEpochNanoseconds().
  Handle<BigInt> ns;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, ns, SystemUTCEpochNanoseconds(isolate),
                             JSTemporalZonedDateTime);
  // Return ? CreateTemporalZonedDateTime(ns, timeZone, calendar).
  return CreateTemporalZonedDateTime(isolate, ns, time_zone, calendar);
}

// #sec-temporal-compareepochnanoseconds
MaybeHandle<Smi> CompareEpochNanoseconds(Isolate* isolate, Handle<BigInt> one,
                                         Handle<BigInt> two) {
  TEMPORAL_ENTER_FUNC

  ComparisonResult result = BigInt::CompareToBigInt(one, two);
  // 1. If epochNanosecondsOne > epochNanosecondsTwo, return 1.
  return Handle<Smi>(
      result != ComparisonResult::kEqual
          ?
          // 2. If epochNanosecondsOne < epochNanosecondsTwo, return -1.
          Smi::FromInt(result == ComparisonResult::kGreaterThan ? 1 : -1)
          :
          // 3. Return 0.
          Smi::zero(),
      isolate);
}

// #sec-temporal-torelativetemporalobject
MaybeHandle<Object> ToRelativeTemporalObject(Isolate* isolate,
                                             Handle<JSReceiver> options,
                                             const char* method) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  // 1. Assert: Type(options) is Object.
  // 2. Let value be ? Get(options, "relativeTo").
  Handle<Object> value_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, value_obj,
      JSReceiver::GetProperty(isolate, options, factory->relativeTo_string()),
      Object);
  // 3. If value is undefined, then
  if (value_obj->IsUndefined()) {
    // a. Return value.
    return value_obj;
  }
  // 4. Let offsetBehaviour be option.
  OffsetBehaviour offset_behaviour = OffsetBehaviour::kOption;

  Handle<Object> time_zone_obj = factory->undefined_value();
  Handle<Object> offset_string_obj = factory->undefined_value();
  DateTimeRecord result;
  Handle<JSReceiver> calendar;
  // 5. If Type(value) is Object, then
  if (value_obj->IsJSReceiver()) {
    Handle<JSReceiver> value = Handle<JSReceiver>::cast(value_obj);
    // a. If value has either an [[InitializedTemporalDateTime]] or
    // [[InitializedTemporalZonedDateTime]] internal slot, then
    if (value->IsJSTemporalPlainDateTime() ||
        value->IsJSTemporalZonedDateTime()) {
      // i. Return value.
      return value;
    }
    // b. If value has an [[InitializedTemporalDate]] internal slot, then
    if (value->IsJSTemporalPlainDate()) {
      Handle<JSTemporalPlainDate> date_value =
          Handle<JSTemporalPlainDate>::cast(value);
      // i. Return ? CreateTemporalDateTime(value.[[ISOYear]],
      // value.[[ISOMonth]], value.[[ISODay]], 0, 0, 0, 0, 0, 0,
      // value.[[Calendar]]).
      return CreateTemporalDateTime(
          isolate, date_value->iso_year(), date_value->iso_month(),
          date_value->iso_day(), 0, 0, 0, 0, 0, 0,
          Handle<JSReceiver>(date_value->calendar(), isolate));
    }
    // c. Let calendar be ? GetTemporalCalendarWithISODefault(value).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        GetTemporalCalendarWithISODefault(isolate, value, method), Object);
    // See https://github.com/tc39/proposal-temporal/pull/1862
    // d. Let fieldNames be ? CalendarFields(calendar, «  "day", "hour",
    // "microsecond", "millisecond", "minute", "month", "monthCode",
    // "nanosecond", "second", "year" »).
    Handle<FixedArray> field_names = factory->NewFixedArray(10);
    field_names->set(0, *(factory->day_string()));
    field_names->set(1, *(factory->hour_string()));
    field_names->set(2, *(factory->microsecond_string()));
    field_names->set(3, *(factory->millisecond_string()));
    field_names->set(4, *(factory->minute_string()));
    field_names->set(5, *(factory->month_string()));
    field_names->set(6, *(factory->monthCode_string()));
    field_names->set(7, *(factory->nanosecond_string()));
    field_names->set(8, *(factory->second_string()));
    field_names->set(9, *(factory->year_string()));
    ASSIGN_RETURN_ON_EXCEPTION(isolate, field_names,
                               CalendarFields(isolate, calendar, field_names),
                               Object);
    // e. Let fields be ? PrepareTemporalFields(value, fieldNames, «»).
    Handle<JSReceiver> fields;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, fields,
        PrepareTemporalFields(isolate, value, field_names, false, false),
        Object);
    // f. Let dateOptions be ! OrdinaryObjectCreate(null).
    Handle<JSObject> date_options = factory->NewJSObjectWithNullProto();
    // g. Perform ! CreateDataPropertyOrThrow(dateOptions, "overflow",
    // "constrain").
    CHECK(JSReceiver::CreateDataProperty(
              isolate, date_options, factory->overflow_string(),
              ShowOverflowToString(isolate, ShowOverflow::kConstrain),
              Just(kThrowOnError))
              .FromJust());
    // h. Let result be ? InterpretTemporalDateTimeFields(calendar, fields,
    // dateOptions).
    Maybe<DateTimeRecord> maybe_result = InterpretTemporalDateTimeFields(
        isolate, calendar, fields, date_options, method);
    MAYBE_RETURN(maybe_result, Handle<Object>());
    result = maybe_result.FromJust();

    // i. Let offsetString be ? Get(value, "offset").
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, offset_string_obj,
        JSReceiver::GetProperty(isolate, value, factory->offset_string()),
        Object);
    // j. Let timeZone be ? Get(value, "timeZone").
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone_obj,
        JSReceiver::GetProperty(isolate, value, factory->timeZone_string()),
        Object);
    // k. If offsetString is undefined, then
    if (offset_string_obj->IsUndefined()) {
      // i. Set offsetBehaviour to wall.
      offset_behaviour = OffsetBehaviour::kWall;
    }

    // 6. Else,
  } else {
    // a. Let string be ? ToString(value).
    Handle<String> string;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, string,
                               Object::ToString(isolate, value_obj), Object);
    // b. Let result be ? ParseTemporalRelativeToString(string).
    Maybe<ZonedDateTimeRecord> maybe_relative_result =
        ParseTemporalRelativeToString(isolate, string);
    MAYBE_RETURN(maybe_relative_result, Handle<Object>());
    ZonedDateTimeRecord relative_result = maybe_relative_result.FromJust();
    result.year = relative_result.year;
    result.month = relative_result.month;
    result.day = relative_result.day;
    result.hour = relative_result.hour;
    result.minute = relative_result.minute;
    result.second = relative_result.second;
    result.millisecond = relative_result.millisecond;
    result.microsecond = relative_result.microsecond;
    result.nanosecond = relative_result.nanosecond;

    // c. Let calendar be ?
    // ToTemporalCalendarWithISODefault(result.[[Calendar]]).
    Handle<Object> result_calendar;
    if (relative_result.calendar.empty()) {
      result_calendar = factory->undefined_value();
    } else {
      result_calendar =
          factory->NewStringFromAsciiChecked(relative_result.calendar.c_str());
    }
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, calendar,
        ToTemporalCalendarWithISODefault(isolate, result_calendar, method),
        Object);

    // d. Let offsetString be result.[[TimeZoneOffset]].
    if (!relative_result.offset_string.empty()) {
      offset_string_obj = factory->NewStringFromAsciiChecked(
          relative_result.offset_string.c_str());
    }

    // e. Let timeZone be result.[[TimeZoneIANAName]].
    if (!relative_result.time_zone_name.empty()) {
      time_zone_obj = factory->NewStringFromAsciiChecked(
          relative_result.time_zone_name.c_str());
    }
    // f. If result.[[TimeZoneZ]] is true, then
    if (relative_result.time_zone_z) {
      // i. Set offsetBehaviour to exact.
      offset_behaviour = OffsetBehaviour::kExact;
      // g. Else if offsetString is undefined, then
    } else if (offset_string_obj->IsUndefined()) {
      // i. Set offsetBehaviour to wall.
      offset_behaviour = OffsetBehaviour::kWall;
    }
  }
  // 7. If timeZone is not undefined, then
  if (!time_zone_obj->IsUndefined()) {
    // a. Set timeZone to ? ToTemporalTimeZone(timeZone).
    Handle<JSReceiver> time_zone;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone, ToTemporalTimeZone(isolate, time_zone_obj, method),
        Object);
    // b. If offsetBehaviour is option, then
    int64_t offset_ns = 0;
    if (offset_behaviour == OffsetBehaviour::kOption) {
      // i. Set offsetString to ? ToString(offsetString).
      Handle<String> offset_string;
      ASSIGN_RETURN_ON_EXCEPTION(isolate, offset_string,
                                 Object::ToString(isolate, offset_string_obj),
                                 Object);
      // ii. Let offsetNs be ? ParseTimeZoneOffsetString(offset_string).
      Maybe<int64_t> maybe_offset_ns =
          ParseTimeZoneOffsetString(isolate, offset_string);
      MAYBE_RETURN(maybe_offset_ns, Handle<Object>());
      offset_ns = maybe_offset_ns.FromJust();
      // c. Else,
    } else {
      // i. Let offsetNs be 0.
      offset_ns = 0;
    }
    // d. Let epochNanoseconds be ? InterpretISODateTimeOffset(result.[[Year]],
    // result.[[Month]], result.[[Day]], result.[[Hour]], result.[[Minute]],
    // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
    // result.[[Nanosecond]], offsetBehaviour, offsetNs, timeZone, "compatible",
    // "reject").
    Handle<BigInt> epoch_nanoseconds;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, epoch_nanoseconds,
        InterpretISODateTimeOffset(
            isolate, result.year, result.month, result.day, result.hour,
            result.minute, result.second, result.millisecond,
            result.microsecond, result.nanosecond, offset_behaviour, offset_ns,
            time_zone, Disambiguation::kCompatible, Offset::kReject, method),
        Object);

    // e. Return ? CreateTemporalZonedDateTime(epochNanoseconds, timeZone,
    // calendar).
    return CreateTemporalZonedDateTime(isolate, epoch_nanoseconds, time_zone,
                                       calendar);
  }
  // 7. Return ? CreateTemporalDateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // calendar).
  return CreateTemporalDateTime(isolate, result.year, result.month, result.day,
                                result.hour, result.minute, result.second,
                                result.millisecond, result.microsecond,
                                result.nanosecond, calendar);
}

namespace {
// #sec-temporal-formatsecondsstringpart
const std::string FormatSecondsStringPart(int32_t second, int32_t millisecond,
                                          int32_t microsecond,
                                          int32_t nanosecond,
                                          temporal::Precision precision) {
  // 1. Assert: second, millisecond, microsecond and nanosecond are integers.
  // 2. If precision is "minute", return "".
  if (precision == temporal::Precision::kMinute) {
    return "";
  }
  // 3. Let secondsString be the string-concatenation of the code unit 0x003A
  // (COLON) and second formatted as a two-digit decimal number, padded to the
  // left with zeroes if necessary.
  std::ostringstream seconds_string;
  seconds_string << ':' << std::setfill('0') << std::setw(2) << second;
  // 4. Let fraction be millisecond × 106 + microsecond × 103 + nanosecond.
  int32_t fraction = millisecond * 1000000 + microsecond * 1000 + nanosecond;

  std::ostringstream fraction_oss;
  int32_t precision_len = 9;
  fraction_oss << std::setfill('0') << std::setw(precision_len)
               << std::to_string(fraction);
  std::string fraction_str = fraction_oss.str();

  // 5. If precision is "auto", then
  if (precision == temporal::Precision::kAuto) {
    // a. If fraction is 0, return secondsString.
    if (fraction == 0) return seconds_string.str();

    // b. Set fraction to fraction formatted as a nine-digit decimal number,
    // padded to the left with zeroes if necessary. c. Set fraction to the
    // longest possible substring of fraction starting at position 0 and not
    // ending with the code unit 0x0030 (DIGIT ZERO).
    while (precision_len > 0 && fraction_str[precision_len - 1] == '0') {
      precision_len--;
    }
  } else {
    // a. If precision is 0, return secondsString.
    // b. Set fraction to fraction formatted as a nine-digit decimal number,
    // padded to the left with zeroes if necessary.
    if (precision == temporal::Precision::k0) return seconds_string.str();
    // c. Set fraction to the substring of fraction from 0 to precision.
    precision_len = static_cast<int32_t>(precision);
  }
  // 7. Return the string-concatenation of secondsString, the code unit 0x002E
  // (FULL STOP), and fraction.
  seconds_string << '.' << fraction_str.substr(0, precision_len);
  return seconds_string.str();
}

MaybeHandle<String> FormatCalendarAnnotation(
    Isolate* isolate, Handle<String> id, temporal::ShowCalendar show_calendar) {
  IncrementalStringBuilder builder(isolate);
  // 1.Assert: showCalendar is "auto", "always", or "never".
  // 2. If showCalendar is "never", return the empty String.
  if (show_calendar == temporal::ShowCalendar::kNever) {
    return builder.Finish();
  }
  // 3. If showCalendar is "auto" and id is "iso8601", return the empty String.
  if (show_calendar == temporal::ShowCalendar::kAuto &&
      String::Equals(isolate, id, isolate->factory()->iso8601_string())) {
    return builder.Finish();
  }
  // 4. Return the string-concatenation of "[u-ca=", id, and "]".
  builder.AppendCString("[u-ca=");
  builder.AppendString(id);
  builder.AppendCString("]");
  return builder.Finish();
}

const std::string PadISOYear(int32_t y) {
  // 1. Assert: y is an integer.
  // 2. If y > 999 and y ≤ 9999, then
  if (y > 999 && y <= 9999) {
    // a. Return y formatted as a four-digit decimal number.
    return std::to_string(y);
  }
  std::ostringstream oss;
  // 3. If y ≥ 0, let yearSign be "+"; otherwise, let yearSign be "-".
  oss << ((y >= 0) ? '+' : '-');
  // 4. Let year be abs(y), formatted as a six-digit decimal number, padded to
  // the left with zeroes as necessary.
  oss << std::setfill('0') << std::setw(6) << std::abs(y);
  // 5. Return the string-concatenation of yearSign and year.
  return oss.str();
}

}  // namespace

MaybeHandle<String> TemporalDurationToString(Isolate* isolate,
                                             const DurationRecord& input,
                                             Precision precision) {
  DurationRecord dur = input;
  // 1. Assert: precision is not "minute".
  CHECK(precision != Precision::kMinute);
  // 2. Set seconds to the mathematical value of seconds.
  // 3. Set milliseconds to the mathematical value of milliseconds.
  // 4. Set microseconds to the mathematical value of microseconds.
  // 5. Set nanoseconds to the mathematical value of nanoseconds.
  // 6. Let sign be ! DurationSign(years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds).
  int32_t sign = DurationSign(isolate, input);

  // 7. Set microseconds to microseconds + the integral part of nanoseconds /
  // 1000.
  dur.microseconds += static_cast<int>(dur.nanoseconds / 1000);
  // 8. Set nanoseconds to nanoseconds modulo 1000.
  dur.nanoseconds = static_cast<int>(dur.nanoseconds) % 1000;
  // 9. Set milliseconds to milliseconds + the integral part of microseconds /
  // 1000.
  dur.milliseconds += static_cast<int>(dur.microseconds / 1000);
  // 10. Set microseconds to microseconds modulo 1000.
  dur.microseconds = static_cast<int>(dur.microseconds) % 1000;
  // 11. Set seconds to seconds + the integral part of milliseconds / 1000.
  dur.seconds += static_cast<int>(dur.milliseconds / 1000);
  // 12. Set milliseconds to milliseconds modulo 1000.
  dur.milliseconds = static_cast<int>(dur.milliseconds) % 1000;
  // 13. Let datePart be "".
  std::ostringstream date_part;
  // 14. If years is not 0, then
  // a. Set datePart to the string concatenation of abs(years) formatted as a
  // decimal number and the code unit 0x0059 (LATIN CAPITAL LETTER Y).
  if (dur.years != 0) {
    date_part << std::fixed << std::setprecision(0) << std::abs(dur.years)
              << 'Y';
  }
  // 15. If months is not 0, then
  // a. Set datePart to the string concatenation of datePart,
  // abs(months) formatted as a decimal number, and the code unit
  // 0x004D (LATIN CAPITAL LETTER M).
  if (dur.months != 0) {
    date_part << std::fixed << std::setprecision(0) << std::abs(dur.months)
              << 'M';
  }
  // 16. If weeks is not 0, then
  // a. Set datePart to the string concatenation of datePart,
  // abs(weeks) formatted as a decimal number, and the code unit
  // 0x0057 (LATIN CAPITAL LETTER W).
  if (dur.weeks != 0) {
    date_part << std::fixed << std::setprecision(0) << std::abs(dur.weeks)
              << 'W';
  }
  // 17. If days is not 0, then
  // a. Set datePart to the string concatenation of datePart,
  // abs(days) formatted as a decimal number, and the code unit 0x0044
  // (LATIN CAPITAL LETTER D).
  if (dur.days != 0) {
    date_part << std::fixed << std::setprecision(0) << std::abs(dur.days)
              << 'D';
  }
  // 18. Let timePart be "".
  std::ostringstream time_part;
  // 19. If hours is not 0, then
  // a. Set timePart to the string concatenation of abs(hours) formatted as a
  // decimal number and the code unit 0x0048 (LATIN CAPITAL LETTER H).
  if (dur.hours != 0) {
    time_part << std::fixed << std::setprecision(0) << std::abs(dur.hours)
              << 'H';
  }
  // 20. If minutes is not 0, then
  // a. Set timePart to the string concatenation of timePart,
  // abs(minutes) formatted as a decimal number, and the code unit
  // 0x004D (LATIN CAPITAL LETTER M).
  if (dur.minutes != 0) {
    time_part << std::fixed << std::setprecision(0) << std::abs(dur.minutes)
              << 'M';
  }
  // 21. If any of seconds, milliseconds, microseconds, and nanoseconds are not
  // 0; or years, months, weeks, days, hours, and minutes are all 0, then
  if ((dur.seconds != 0 || dur.milliseconds != 0 || dur.microseconds != 0 ||
       dur.nanoseconds != 0) ||
      (dur.years == 0 && dur.months == 0 && dur.weeks == 0 && dur.days == 0 &&
       dur.hours == 0 && dur.minutes == 0)) {
    // a. Let fraction be abs(milliseconds) × 106 + abs(microseconds) × 103 +
    // abs(nanoseconds).
    int32_t fraction = std::abs(dur.milliseconds) * 1000000 +
                       std::abs(dur.microseconds) * 1000 +
                       std::abs(dur.nanoseconds);
    // b. Let decimalPart be fraction formatted as a nine-digit decimal number,
    // padded to the left with zeroes if necessary.
    std::ostringstream decimal_parts_oss;
    int32_t precision_len = 9;
    decimal_parts_oss << std::setfill('0') << std::setw(precision_len)
                      << std::to_string(fraction);
    std::string decimal_parts = decimal_parts_oss.str();

    // c. If precision is "auto", then
    if (precision == Precision::kAuto) {
      // i. Set decimalPart to the longest possible substring of decimalPart
      // starting at position 0 and not ending with the code unit 0x0030 (DIGIT
      // ZERO).
      while (precision_len > 0 && decimal_parts[precision_len - 1] == '0') {
        precision_len--;
      }
      // See https://github.com/tc39/proposal-temporal/pull/1860
      // d. Else if _precision_ = 0, then
    } else if (precision == Precision::k0) {
      precision_len = 0;
    } else {
      // d. Else if precision ≠ 0, then
      // i. Set decimalPart to the substring of decimalPart from 0 to precision.
      precision_len = static_cast<int32_t>(precision);
    }
    decimal_parts = decimal_parts.substr(0, precision_len);
    // e. Let secondsPart be abs(seconds) formatted as a decimal number.
    time_part << std::fixed << std::setprecision(0) << std::abs(dur.seconds);
    // f. If decimalPart is not "", then
    if (decimal_parts.length() > 0) {
      // i. Set secondsPart to the string-concatenation of secondsPart, the code
      // unit 0x002E (FULL STOP), and decimalPart.
      time_part << '.' << decimal_parts;
    }
    // g. Set timePart to the string concatenation of timePart, secondsPart, and
    // the code unit 0x0053 (LATIN CAPITAL LETTER S).
    time_part << 'S';
  }
  // 22. Let signPart be the code unit 0x002D (HYPHEN-MINUS) if sign < 0, and
  // otherwise the empty String.
  // 23. Let result be the string concatenation of signPart, the code unit
  // 0x0050 (LATIN CAPITAL LETTER P) and datePart.
  std::ostringstream result;
  result << ((sign < 0) ? "-P" : "P") << date_part.str();
  // 24. If timePart is not "", then
  if (time_part.str().length() > 0) {
    // a. Set result to the string concatenation of result, the code unit 0x0054
    // (LATIN CAPITAL LETTER T), and timePart.
    result << 'T' << time_part.str();
  }
  return isolate->factory()->NewStringFromAsciiChecked(result.str().c_str());
}

MaybeHandle<String> TemporalDateToString(Isolate* isolate, int32_t iso_year,
                                         int32_t iso_month, int32_t iso_day,
                                         Handle<JSReceiver> calendar,
                                         ShowCalendar show_calendar) {
  // 1. Assert: Type(temporalDate) is Object.
  // 2. Assert: temporalDate has an [[InitializedTemporalDate]] internal slot.
  // 6. Let calendarID be ? ToString(temporalDate.[[Calendar]]).
  Handle<String> calendar_id;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar_id,
                             Object::ToString(isolate, calendar), String);
  std::ostringstream result;
  // 3. Let year be ! PadISOYear(temporalDate.[[ISOYear]]).
  result << PadISOYear(iso_year)
         // 4. Let month be temporalDate.[[ISOMonth]] formatted as a two-digit
         // decimal number, padded to the left with a zero if necessary.
         << "-" << std::setfill('0') << std::setw(2)
         << iso_month
         // 5. Let day be temporalDate.[[ISODay]] formatted as a two-digit
         // decimal number, padded to the left with a zero if necessary.
         << "-" << std::setfill('0') << std::setw(2) << iso_day;

  IncrementalStringBuilder builder(isolate);
  builder.AppendCString(result.str().c_str());

  // 7. Let calendar be ! FormatCalendarAnnotation(calendarID,
  // showCalendar).
  Handle<String> calendar_string;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_string,
      FormatCalendarAnnotation(isolate, calendar_id, show_calendar), String);
  // 8. Return the string-concatenation of year, the code unit 0x002D
  // (HYPHEN-MINUS), month, the code unit 0x002D (HYPHEN-MINUS), day, and
  // calendar.
  builder.AppendString(calendar_string);
  return builder.Finish();
}

MaybeHandle<String> TemporalTimeToString(Isolate* isolate, int32_t hour,
                                         int32_t minute, int32_t second,
                                         int32_t millisecond,
                                         int32_t microsecond,
                                         int32_t nanosecond,
                                         Precision precision) {
  // 1. Assert: hour, minute, second, millisecond, microsecond and nanosecond
  // are integers.
  std::ostringstream result;
  // 2. Let hour be hour formatted as a two-digit decimal number, padded to the
  // left with a zero if necessary.
  // 3. Let minute be minute formatted as a two-digit decimal number, padded to
  // the left with a zero if necessary.
  // 4. Let seconds be ! FormatSecondsStringPart(second, millisecond,
  // microsecond, nanosecond, precision).
  // 5. Return the string-concatenation of hour, the code unit 0x003A (COLON),
  // minute, and seconds.
  result << std::setfill('0') << std::setw(2) << hour << ':'
         << std::setfill('0') << std::setw(2) << minute
         << FormatSecondsStringPart(second, millisecond, microsecond,
                                    nanosecond, precision);
  return isolate->factory()->NewStringFromAsciiChecked(result.str().c_str());
}

MaybeHandle<String> TemporalDateTimeToString(
    Isolate* isolate, int32_t iso_year, int32_t iso_month, int32_t iso_day,
    int32_t hour, int32_t minute, int32_t second, int32_t millisecond,
    int32_t microsecond, int32_t nanosecond, Handle<JSReceiver> calendar,
    Precision precision, ShowCalendar show_calendar) {
  // 8. Let calendarID be ? ToString(calendar).
  Handle<String> calendar_id;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar_id,
                             Object::ToString(isolate, calendar), String);
  // 1. Assert: isoYear, isoMonth, isoDay, hour, minute, second, millisecond,
  // microsecond, and nanosecond are integers.
  std::ostringstream result;
  // 2. Let year be ! PadISOYear(isoYear).
  result << PadISOYear(iso_year)
         // 3. Let month be isoMonth formatted as a two-digit decimal number,
         // padded to the left with a zero if necessary.
         << '-' << std::setfill('0') << std::setw(2)
         << iso_month
         // 4. Let day be isoDay formatted as a two-digit decimal number, padded
         // to the left with a zero if necessary.
         << '-' << std::setfill('0') << std::setw(2)
         << iso_day
         // 5. Let hour be hour formatted as a two-digit decimal number, padded
         // to the left with a zero if necessary.
         << 'T' << std::setfill('0') << std::setw(2) << hour
         << ':'
         // 6. Let minute be minute formatted as a two-digit decimal number,
         // padded to the left with a zero if necessary.
         << std::setfill('0') << std::setw(2)
         << minute
         // 7. Let seconds be ! FormatSecondsStringPart(second, millisecond,
         // microsecond, nanosecond, precision).
         << FormatSecondsStringPart(second, millisecond, microsecond,
                                    nanosecond, precision);

  IncrementalStringBuilder builder(isolate);
  builder.AppendCString(result.str().c_str());

  // 9. Let calendarString be ! FormatCalendarAnnotation(calendarID,
  // showCalendar).
  Handle<String> calendar_string;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_string,
      FormatCalendarAnnotation(isolate, calendar_id, show_calendar), String);

  // 10. Return the string-concatenation of year, the code unit 0x002D
  // (HYPHEN-MINUS), month, the code unit 0x002D (HYPHEN-MINUS), day, 0x0054
  // (LATIN CAPITAL LETTER T), hour, the code unit 0x003A (COLON), minute,
  builder.AppendString(calendar_string);
  return builder.Finish();
}

MaybeHandle<String> TemporalYearMonthToString(
    Isolate* isolate, int32_t iso_year, int32_t iso_month, int32_t iso_day,
    Handle<JSReceiver> calendar, ShowCalendar show_calendar) {
  Handle<String> calendar_id;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar_id,
                             Object::ToString(isolate, calendar), String);
  // 1. Assert: Type(yearMonth) is Object.
  // 2. Assert: yearMonth has an [[InitializedTemporalYearMonth]] internal slot.
  std::ostringstream result;
  // 3. Let year be ! PadISOYear(yearMonth.[[ISOYear]]).
  result << PadISOYear(iso_year)
         // 4. Let month be yearMonth.[[ISOMonth]] formatted as a two-digit
         // decimal number, padded to the left with a zero if necessary.
         // 5. Let result be the string-concatenation of year, the code unit
         // 0x002D (HYPHEN-MINUS), and month.
         << '-' << std::setfill('0') << std::setw(2) << iso_month;
  // 6. Let calendarID be ? ToString(yearMonth.[[Calendar]]).
  // 7. If calendarID is not "iso8601", then
  if (!String::Equals(isolate, calendar_id,
                      isolate->factory()->iso8601_string())) {
    // a. Let day be yearMonth.[[ISODay]] formatted as a two-digit decimal
    // number, padded to the left with a zero if necessary. b. Set result to the
    // string-concatenation of result, the code unit 0x002D (HYPHEN-MINUS), and
    // day.
    result << '-' << std::setfill('0') << std::setw(2) << iso_day;
  }
  IncrementalStringBuilder builder(isolate);
  builder.AppendCString(result.str().c_str());
  // 8. Let calendarString be ! FormatCalendarAnnotation(calendarID,
  // showCalendar).
  Handle<String> calendar_string;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_string,
      FormatCalendarAnnotation(isolate, calendar_id, show_calendar), String);

  // 9. If calendarString is not the empty String, then
  // a. Set result to the string-concatenation of result and calendarString.
  builder.AppendString(calendar_string);

  // 10. Return result.
  return builder.Finish();
}

MaybeHandle<String> TemporalMonthDayToString(Isolate* isolate, int32_t iso_year,
                                             int32_t iso_month, int32_t iso_day,
                                             Handle<JSReceiver> calendar,
                                             ShowCalendar show_calendar) {
  Handle<String> calendar_id;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar_id,
                             Object::ToString(isolate, calendar), String);
  // 1. Assert: Type(monthDay) is Object.
  // 2. Assert: monthDay has an [[InitializedTemporalMonthDay]] internal slot.
  std::ostringstream result;
  // 3. Let month be monthDay.[[ISOMonth]] formatted as a two-digit decimal
  // number, padded to the left with a zero if necessary.
  // 4. Let day be monthDay.[[ISODay]] formatted as a two-digit decimal number,
  // padded to the left with a zero if necessary.
  // 5. Let result be the string-concatenation of month, the code unit 0x002D
  // (HYPHEN-MINUS), and day.
  // 6. Let calendarID be ? ToString(monthDay.[[Calendar]]).
  // 7. If calendarID is not "iso8601", then
  if (!String::Equals(isolate, calendar_id,
                      isolate->factory()->iso8601_string())) {
    // a. Let year be ! PadISOYear(monthDay.[[ISOYear]]).
    result << PadISOYear(iso_year)
           // b. Set result to the string-concatenation of year, the code unit
           // 0x002D (HYPHEN-MINUS), and result.
           << '-';
  }
  result << std::setfill('0') << std::setw(2) << iso_month << "-"
         << std::setfill('0') << std::setw(2) << iso_day;
  IncrementalStringBuilder builder(isolate);
  builder.AppendCString(result.str().c_str());
  // 8. Let calendarString be ! FormatCalendarAnnotation(calendarID,
  // showCalendar).
  Handle<String> calendar_string;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_string,
      FormatCalendarAnnotation(isolate, calendar_id, show_calendar), String);
  // 9. If calendarString is not the empty String, then
  // a. Set result to the string-concatenation of result and calendarString.
  builder.AppendString(calendar_string);
  // 10. Return result.
  return builder.Finish();
}

MaybeHandle<String> TemporalInstantToString(Isolate* isolate,
                                            Handle<JSTemporalInstant> instant,
                                            Handle<Object> time_zone_obj,
                                            Precision precision) {
  // 1. Assert: Type(instant) is Object.
  // 2. Assert: instant has an [[InitializedTemporalInstant]] internal slot.
  // 3. Let outputTimeZone be timeZone.
  Handle<JSReceiver> output_time_zone;

  // 4. If outputTimeZone is undefined, then
  if (time_zone_obj->IsUndefined()) {
    // a. Set outputTimeZone to ? CreateTemporalTimeZone("UTC").
    ASSIGN_RETURN_ON_EXCEPTION(isolate, output_time_zone,
                               CreateTemporalTimeZoneUTC(isolate), String);
  } else {
    CHECK(time_zone_obj->IsJSReceiver());
    output_time_zone = Handle<JSReceiver>::cast(time_zone_obj);
  }

  // 5. Let isoCalendar be ? GetISO8601Calendar().
  Handle<JSTemporalCalendar> iso_calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, iso_calendar, GetISO8601Calendar(isolate),
                             String);
  // 6. Let dateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(outputTimeZone,
  // instant, isoCalendar).
  Handle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      BuiltinTimeZoneGetPlainDateTimeFor(isolate, output_time_zone, instant,
                                         iso_calendar),
      String);
  // 7. Let dateTimeString be ? TemporalDateTimeToString(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]], undefined, precision, "never").

  Handle<String> date_time_string;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time_string,
      TemporalDateTimeToString(
          isolate, date_time->iso_year(), date_time->iso_month(),
          date_time->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
          date_time->iso_second(), date_time->iso_millisecond(),
          date_time->iso_microsecond(), date_time->iso_nanosecond(),
          iso_calendar,  // Unimportant due to ShowCalendar::kNever
          precision, ShowCalendar::kNever),
      String);

  std::ostringstream result;
  // 8. If timeZone is undefined, then
  if (time_zone_obj->IsUndefined()) {
    // a. Let timeZoneString be "Z".
    result << 'Z';
  } else {
    // 9. Else,
    // a. Let timeZoneString be ? BuiltinTimeZoneGetOffsetStringFor(timeZone,
    // instant).
    CHECK(time_zone_obj->IsJSReceiver());
    Handle<JSReceiver> time_zone = Handle<JSReceiver>::cast(time_zone_obj);
    Maybe<std::string> maybe_time_zone_string =
        BuiltinTimeZoneGetOffsetStdStringFor(isolate, time_zone, instant);
    MAYBE_RETURN(maybe_time_zone_string, Handle<String>());
    result << maybe_time_zone_string.FromJust();
  }
  IncrementalStringBuilder builder(isolate);

  builder.AppendString(date_time_string);
  builder.AppendCString(result.str().c_str());

  // 10. Return the string-concatenation of dateTimeString and timeZoneString.
  return builder.Finish();
}

// #sec-temporal-temporalzoneddatetimetostring
MaybeHandle<String> TemporalZonedDateTimeToString(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Precision precision, ShowCalendar show_calendar,
    ShowTimeZone show_time_zone, ShowOffset show_offset, double increment,
    Unit unit, RoundingMode rounding_mode) {
  std::ostringstream output;
  // 5. Let ns be ? RoundTemporalInstant(zonedDateTime.[[Nanoseconds]],
  // increment, unit, roundingMode).
  Handle<BigInt> ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, ns,
      RoundTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate),
          increment, unit, rounding_mode),
      String);

  // 6. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 7. Let instant be ! CreateTemporalInstant(ns).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, instant,
                             CreateTemporalInstant(isolate, ns), String);

  // 8. Let isoCalendar be ! GetISO8601Calendar().
  Handle<JSTemporalCalendar> iso_calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, iso_calendar, GetISO8601Calendar(isolate),
                             String);

  // 9. Let temporalDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, isoCalendar).
  Handle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, temporal_date_time,
                             BuiltinTimeZoneGetPlainDateTimeFor(
                                 isolate, time_zone, instant, iso_calendar),
                             String);
  // 10. Let dateTimeString be ?
  // TemporalDateTimeToString(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]],
  // temporalDateTime.[[ISOHour]], temporalDateTime.[[ISOMinute]],
  // temporalDateTime.[[ISOSecond]], temporalDateTime.[[ISOMillisecond]],
  // temporalDateTime.[[ISOMicrosecond]], temporalDateTime.[[ISONanosecond]],
  // isoCalendar, precision, "never").
  Handle<String> date_time_string;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time_string,
      TemporalDateTimeToString(
          isolate, temporal_date_time->iso_year(),
          temporal_date_time->iso_month(), temporal_date_time->iso_day(),
          temporal_date_time->iso_hour(), temporal_date_time->iso_minute(),
          temporal_date_time->iso_second(),
          temporal_date_time->iso_millisecond(),
          temporal_date_time->iso_microsecond(),
          temporal_date_time->iso_nanosecond(), iso_calendar, precision,
          ShowCalendar::kNever),
      String);
  IncrementalStringBuilder builder(isolate);
  builder.AppendString(date_time_string);

  // 11. If showOffset is "never", then
  if (show_offset == ShowOffset::kNever) {
    // a. Let offsetString be the empty String.
    // 12. Else,
  } else {
    // a. Let offsetString be ? BuiltinTimeZoneGetOffsetStringFor(timeZone,
    // instant).
    Maybe<std::string> maybe_offset_string =
        BuiltinTimeZoneGetOffsetStdStringFor(isolate, time_zone, instant);
    MAYBE_RETURN(maybe_offset_string, Handle<String>());
    output << maybe_offset_string.FromJust();
  }

  builder.AppendCString(output.str().c_str());
  // 13. If showTimeZone is "never", then
  if (show_time_zone == ShowTimeZone::kNever) {
    // a. Let timeZoneString be the empty String.
    // 14. Else,
  } else {
    // a. Let timeZoneID be ? ToString(timeZone).
    Handle<String> time_zone_id;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, time_zone_id,
                               Object::ToString(isolate, time_zone), String);
    // b. Let timeZoneString be the string-concatenation of the code unit 0x005B
    // (LEFT SQUARE BRACKET), timeZoneID, and the code unit 0x005D (RIGHT SQUARE
    // BRACKET).
    builder.AppendCString("[");
    builder.AppendString(time_zone_id);
    builder.AppendCString("]");
  }
  // 15. Let calendarID be ? ToString(zonedDateTime.[[Calendar]]).
  Handle<String> calendar_id;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_id,
      Object::ToString(
          isolate, Handle<JSReceiver>(zoned_date_time->calendar(), isolate)),
      String);

  // 16. Let calendarString be ! FormatCalendarAnnotation(calendarID,
  // showCalendar).
  Handle<String> calendar_string;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_string,
      FormatCalendarAnnotation(isolate, calendar_id, show_calendar), String);
  builder.AppendString(calendar_string);
  // 17. Return the string-concatenation of dateTimeString, offsetString,
  // timeZoneString, and calendarString.
  return builder.Finish();
}

// #sec-temporal-temporalzoneddatetimetostring
MaybeHandle<String> TemporalZonedDateTimeToString(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Precision precision, ShowCalendar show_calendar,
    ShowTimeZone show_time_zone, ShowOffset show_offset) {
  // 1. Assert: Type(zonedDateTime) is Object and zonedDateTime has an
  // [[InitializedTemporalZonedDateTime]] internal slot.
  // 2. If increment is not present, set it to 1.
  // 3. If unit is not present, set it to "nanosecond".
  // 4. If roundingMode is not present, set it to "trunc".
  return TemporalZonedDateTimeToString(
      isolate, zoned_date_time, precision, show_calendar, show_time_zone,
      show_offset, 1, Unit::kNanosecond, RoundingMode::kTrunc);
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
  TEMPORAL_ENTER_FUNC
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

Maybe<DateTimeRecord> ParseISODateTime(Isolate* isolate,
                                       const ParsedResult& parsed) {
  TEMPORAL_ENTER_FUNC

  DateTimeRecord result;
  // 5. Set year to ! ToIntegerOrInfinity(year).
  result.year = StringToInt(parsed.date_year);
  // 6. If month is undefined, then
  if (parsed.date_month.empty()) {
    // a. Set month to 1.
    result.month = 1;
    // 7. Else,
  } else {
    // a. Set month to ! ToIntegerOrInfinity(month).
    result.month = StringToInt(parsed.date_month);
  }

  // 8. If day is undefined, then
  if (parsed.date_day.empty()) {
    // a. Set day to 1.
    result.day = 1;
    // 9. Else,
  } else {
    // a. Set day to ! ToIntegerOrInfinity(day).
    result.day = StringToInt(parsed.date_day);
  }
  // 10. Set hour to ! ToIntegerOrInfinity(hour).
  result.hour = StringToInt(parsed.time_hour);
  // 11. Set minute to ! ToIntegerOrInfinity(minute).
  result.minute = StringToInt(parsed.time_minute);
  // 12. Set second to ! ToIntegerOrInfinity(second).
  result.second = StringToInt(parsed.time_second);
  // 13. If second is 60, then
  if (result.second == 60) {
    // a. Set second to 59.
    result.second = 59;
  }
  // 14. If fraction is not undefined, then
  if (!parsed.time_fractional_part.empty()) {
    // a. Set fraction to the string-concatenation of the previous value of
    // fraction and the string "000000000".
    std::string fraction = parsed.time_fractional_part + "000000000";
    // b. Let millisecond be the String value equal to the substring of fraction
    // from 0 to 3. c. Set millisecond to ! ToIntegerOrInfinity(millisecond).
    result.millisecond = StringToInt(fraction.substr(0, 3));
    // d. Let microsecond be the String value equal to the substring of fraction
    // from 3 to 6. e. Set microsecond to ! ToIntegerOrInfinity(microsecond).
    result.microsecond = StringToInt(fraction.substr(3, 3));
    // f. Let nanosecond be the String value equal to the substring of fraction
    // from 6 to 9. g. Set nanosecond to ! ToIntegerOrInfinity(nanosecond).
    result.nanosecond = StringToInt(fraction.substr(6, 3));
    // 15. Else,
  } else {
    // a. Let millisecond be 0.
    result.millisecond = 0;
    // b. Let microsecond be 0.
    result.microsecond = 0;
    // c. Let nanosecond be 0.
    result.nanosecond = 0;
  }
  // 16. If ! IsValidISODate(year, month, day) is false, throw a RangeError
  // exception.
  if (!IsValidISODate(isolate, result.year, result.month, result.day)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                                 Nothing<DateTimeRecord>());
  }
  // 17. If ! IsValidTime(hour, minute, second, millisecond, microsecond,
  // nanosecond) is false, throw a RangeError exception.
  if (!IsValidTime(isolate, result.hour, result.minute, result.second,
                   result.millisecond, result.microsecond, result.nanosecond)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                                 Nothing<DateTimeRecord>());
  }
  // 18. Return the Record { [[Year]]: year, [[Month]]: month, [[Day]]: day,
  // [[Hour]]: hour, [[Minute]]: minute, [[Second]]: second, [[Millisecond]]:
  // millisecond, [[Microsecond]]: microsecond, [[Nanosecond]]: nanosecond,
  // [[Calendar]]: calendar }.
  result.calendar = parsed.calendar_name;
  return Just(result);
}

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

  while ((s + length < str.length()) && (length < 8) &&
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

// #sec-temporal-parsetemporaldatetimestring
Maybe<DateTimeRecord> ParseTemporalDateTimeString(Isolate* isolate,
                                                  Handle<String> iso_string) {
  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalDateTimeString
  // (see 13.33), then
  ParsedResult parsed;
  SATISFY_STRING_OR_THROW(TemporalDateTimeString, DateTimeRecord, parsed)
  // 3. Let result be ? ParseISODateTime(isoString).
  // 4. Return result.
  return ParseISODateTime(isolate, parsed);
}

// #sec-temporal-parsetemporaldatestring
Maybe<DateRecord> ParseTemporalDateString(Isolate* isolate,
                                          Handle<String> iso_string) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalDateString
  // (see 13.33), then
  ParsedResult parsed;
  SATISFY_STRING_OR_THROW(TemporalDateString, DateRecord, parsed)
  // 3. Let result be ? ParseISODateTime(isoString).
  Maybe<DateTimeRecord> maybe_result = ParseISODateTime(isolate, parsed);

  MAYBE_RETURN(maybe_result, Nothing<DateRecord>());
  DateTimeRecord result = maybe_result.FromJust();
  // 4. Return the Record { [[Year]]: result.[[Year]], [[Month]]:
  // result.[[Month]], [[Day]]: result.[[Day]], [[Calendar]]:
  // result.[[Calendar]] }.
  DateRecord ret = {result.year, result.month, result.day, result.calendar};
  return Just(ret);
}

// #sec-temporal-parsetemporaltimestring
Maybe<TimeRecord> ParseTemporalTimeString(Isolate* isolate,
                                          Handle<String> iso_string) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalTimeString
  // (see 13.33), then
  ParsedResult parsed;
  SATISFY_STRING_OR_THROW(TemporalTimeString, TimeRecord, parsed)
  // 3. Let result be ? ParseISODateTime(isoString).
  Maybe<DateTimeRecord> maybe_result = ParseISODateTime(isolate, parsed);

  MAYBE_RETURN(maybe_result, Nothing<TimeRecord>());
  DateTimeRecord result = maybe_result.FromJust();
  // 4. Return the Record { [[Hour]]: result.[[Hour]], [[Minute]]:
  // result.[[Minute]], [[Second]]: result.[[Second]], [[Millisecond]]:
  // result.[[Millisecond]], [[Microsecond]]: result.[[Microsecond]],
  // [[Nanosecond]]: result.[[Nanosecond]], [[Calendar]]: result.[[Calendar]] }.
  TimeRecord ret = {result.hour,        result.minute,      result.second,
                    result.millisecond, result.microsecond, result.nanosecond,
                    result.calendar};
  return Just(ret);
}

// #sec-temporal-parsetemporalyearmonthstring
Maybe<DateRecord> ParseTemporalYearMonthString(Isolate* isolate,
                                               Handle<String> iso_string) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalYearMonthString
  // (see 13.33), then
  ParsedResult parsed;
  SATISFY_STRING_OR_THROW(TemporalYearMonthString, DateRecord, parsed)
  // 3. Let result be ? ParseISODateTime(isoString).
  Maybe<DateTimeRecord> maybe_result = ParseISODateTime(isolate, parsed);

  MAYBE_RETURN(maybe_result, Nothing<DateRecord>());
  DateTimeRecord result = maybe_result.FromJust();
  // 4. Return the Record { [[Year]]: result.[[Year]], [[Month]]:
  // result.[[Month]], [[Day]]: result.[[Day]], [[Calendar]]:
  // result.[[Calendar]] }.
  DateRecord ret = {result.year, result.month, result.day, result.calendar};
  return Just(ret);
}

// #sec-temporal-parsetemporalmonthdaystring
Maybe<DateRecord> ParseTemporalMonthDayString(Isolate* isolate,
                                              Handle<String> iso_string) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalMonthDayString
  // (see 13.33), then
  ParsedResult parsed;
  SATISFY_STRING_OR_THROW(TemporalMonthDayString, DateRecord, parsed)
  // 3. Let year, month and day be the parts of isoString produced respectively
  // by the DateYear, DateMonth and DateDay productions, or undefined if not
  // present.
  // 4. If year is not undefined, then
  // a. Set year to ! ToIntegerOrInfinity(year).
  int32_t year =
      (!parsed.date_year.empty()) ? StringToInt(parsed.date_year) : kMinInt31;

  // 5. If month is undefined, then
  // a. Set month to 1.
  // 6. Else,
  // a. Set month to ! ToIntegerOrInfinity(month).
  int32_t month =
      (parsed.date_month.empty()) ? 1 : StringToInt(parsed.date_month);

  // 7. If day is undefined, then
  // a. Set day to 1.
  // 8. Else,
  // a. Set day to ! ToIntegerOrInfinity(day).
  int32_t day = (parsed.date_day.empty()) ? 1 : StringToInt(parsed.date_day);
  // 9. Return the Record { [[Year]]: year, [[Month]]: month, [[Day]]: day }.
  DateRecord ret({year, month, day, ""});
  return Just(ret);
}

namespace {

// #sec-temporal-parsetemporalinstantstring
Maybe<InstantRecord> ParseTemporalInstantString(Isolate* isolate,
                                                Handle<String> iso_string) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalInstantString
  // (see 13.33), then
  ParsedResult parsed;
  SATISFY_STRING_OR_THROW(TemporalInstantString, InstantRecord, parsed)

  // 3. Let result be ! ParseISODateTime(isoString).
  Maybe<DateTimeRecord> maybe_result = ParseISODateTime(isolate, parsed);

  MAYBE_RETURN(maybe_result, Nothing<InstantRecord>());
  DateTimeRecord result = maybe_result.FromJust();

  // 4. Let timeZoneResult be ? ParseTemporalTimeZoneString(isoString).
  Maybe<TimeZoneRecord> maybe_time_zone_result =
      ParseTemporalTimeZoneString(isolate, iso_string);
  MAYBE_RETURN(maybe_time_zone_result, Nothing<InstantRecord>());
  TimeZoneRecord time_zone_result = maybe_time_zone_result.FromJust();
  // 5. Let offsetString be timeZoneResult.[[OffsetString]].
  std::string offset_string = time_zone_result.offset_string;
  // 6. If timeZoneResult.[[Z]] is true, then
  if (time_zone_result.z) {
    // a. Set offsetString to "+00:00".
    offset_string = "+00:00";
  }
  // 7. Assert: offsetString is not undefined.
  CHECK(!offset_string.empty());

  // 6. Return the new Record { [[Year]]: result.[[Year]],
  // [[Month]]: result.[[Month]], [[Day]]: result.[[Day]],
  // [[Hour]]: result.[[Hour]], [[Minute]]: result.[[Minute]],
  // [[Second]]: result.[[Second]],
  // [[Millisecond]]: result.[[Millisecond]],
  // [[Microsecond]]: result.[[Microsecond]],
  // [[Nanosecond]]: result.[[Nanosecond]],
  // [[TimeZoneOffsetString]]: offsetString }.
  InstantRecord record;
  record.year = result.year;
  record.month = result.month;
  record.day = result.day;
  record.hour = result.hour;
  record.minute = result.minute;
  record.second = result.second;
  record.millisecond = result.millisecond;
  record.microsecond = result.microsecond;
  record.nanosecond = result.nanosecond;
  record.offset_string = offset_string;
  return Just(record);
}

}  // namespace

// #sec-temporal-parsetemporalzoneddatetimestring
Maybe<ZonedDateTimeRecord> ParseTemporalZonedDateTimeString(
    Isolate* isolate, Handle<String> iso_string) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a
  // TemporalZonedDateTimeString (see 13.33), then
  ParsedResult parsed;
  SATISFY_STRING_OR_THROW(TemporalZonedDateTimeString, ZonedDateTimeRecord,
                          parsed)
  // 3. Let result be ! ParseISODateTime(isoString).
  Maybe<DateTimeRecord> maybe_result = ParseISODateTime(isolate, parsed);

  MAYBE_RETURN(maybe_result, Nothing<ZonedDateTimeRecord>());
  DateTimeRecord result = maybe_result.FromJust();
  // 4. Let timeZoneResult be ? ParseTemporalTimeZoneString(isoString).
  Maybe<TimeZoneRecord> maybe_time_zone_result =
      ParseTemporalTimeZoneString(isolate, iso_string);
  MAYBE_RETURN(maybe_time_zone_result, Nothing<ZonedDateTimeRecord>());
  TimeZoneRecord time_zone_result = maybe_time_zone_result.FromJust();
  // 5. Return the Record { [[Year]]: result.[[Year]], [[Month]]:
  // result.[[Month]], [[Day]]: result.[[Day]], [[Hour]]: result.[[Hour]],
  // [[Minute]]: result.[[Minute]], [[Second]]: result.[[Second]],
  // [[Millisecond]]: result.[[Millisecond]], [[Microsecond]]:
  // result.[[Microsecond]], [[Nanosecond]]: result.[[Nanosecond]],
  // [[Calendar]]: result.[[Calendar]],
  // [[TimeZoneZ]]: timeZoneResult.[[Z]], [[TimeZoneOffsetString]]:
  // timeZoneResult.[[OffsetString]], [[TimeZoneName]]: timeZoneResult.[[Name]]
  // }.
  ZonedDateTimeRecord record;
  record.year = result.year;
  record.month = result.month;
  record.day = result.day;
  record.hour = result.hour;
  record.minute = result.minute;
  record.second = result.second;
  record.millisecond = result.millisecond;
  record.microsecond = result.microsecond;
  record.nanosecond = result.nanosecond;
  record.calendar = result.calendar;
  record.offset_string = time_zone_result.offset_string;
  record.time_zone_name = time_zone_result.name;
  record.time_zone_z = time_zone_result.z;
  return Just(record);
}

namespace {

// #sec-temporal-durationhandlefractions
Maybe<bool> DurationHandleFraction(Isolate* isolate, double f_hours,
                                   double* out_minutes, double f_minutes,
                                   double* out_seconds,
                                   double* out_milliseconds,
                                   double* out_microseconds,
                                   double* out_nanoseconds) {
  TEMPORAL_ENTER_FUNC

  // 1. If fHours is not equal to 0, then
  if (f_hours != 0.0) {
    // a. If any of minutes, fMinutes, seconds, milliseconds, microseconds,
    // nanoseconds is not 0, throw a RangeError exception.
    if (*out_minutes != 0 || f_minutes != 0 || *out_seconds != 0 ||
        *out_milliseconds != 0 || *out_microseconds != 0 ||
        *out_nanoseconds != 0) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), Nothing<bool>());
    }
    // b. Let mins be fHours × 60.
    double mins = f_hours * 60;
    // c. Set minutes to floor(mins).
    *out_minutes = FLOOR(mins);
    // d. Set fMinutes to remainder(mins, 1).
    // TODO(ftang) double check the math.
    f_minutes = mins - *out_minutes;
  }
  // 2. If fMinutes is not equal to 0, then
  if (f_minutes != 0.0) {
    // a. If any of seconds, milliseconds, microseconds, nanoseconds is not 0,
    // throw a RangeError exception.
    if (*out_seconds != 0 || *out_milliseconds != 0 || *out_microseconds != 0 ||
        *out_nanoseconds != 0) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), Nothing<bool>());
    }
    // b. Let secs be fMinutes × 60.
    double secs = f_minutes * 60;
    // c. Set seconds to floor(secs).
    *out_seconds = FLOOR(secs);
    // d. Set fSeconds to secs modulo 1.
    // TODO(ftang) double check the math.
    double f_seconds = secs - *out_seconds;

    // e. If fSeconds is not equal to 0, then
    if (f_seconds != 0.0) {
      // i. Let mils be fSeconds × 1000.
      double mils = f_seconds * 1000;
      // ii. Set milliseconds to floor(mils).
      *out_milliseconds = FLOOR(mils);
      // iii. Let fMilliseconds be remainder(mils, 1).
      double f_milliseconds = mils - *out_milliseconds;

      // iv. If fMilliseconds is not equal to 0, then
      if (f_milliseconds != 0.0) {
        // 1. Let mics be fMilliseconds × 1000.
        double mics = f_milliseconds * 1000;
        // 2. Set microseconds to floor(mics).
        *out_microseconds = FLOOR(mics);
        // 3. Let fMicroseconds be remainder(mics, 1).
        double f_microseconds = mics - *out_microseconds;
        // 4. If fMicroseconds is not equal to 0, then
        if (f_microseconds != 0.0) {
          // a. Let nans be fMicroseconds × 1000.
          double nans = f_microseconds * 1000;
          // b. Set nanoseconds to floor(nans).
          *out_nanoseconds = FLOOR(nans);
        }
      }
    }
  }
  // 3. Return { [[Minutes]]: minutes, [[Seconds]]: seconds, [[Milliseconds]]:
  // milliseconds, [[Microseconds]]: microseconds, [[Nanoseconds]]: nanoseconds
  // }.
  return Just(true);
}

}  // namespace

// #sec-temporal-parsetemporaldurationstring
Maybe<DurationRecord> ParseTemporalDurationString(Isolate* isolate,
                                                  Handle<String> iso_string) {
  TEMPORAL_ENTER_FUNC

  DurationRecord result;
  result.years = result.months = result.weeks = result.days = result.hours =
      result.minutes = result.seconds = result.milliseconds =
          result.microseconds = result.nanoseconds = 6;
  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalDurationString
  // (see 13.33), then
  ParsedDuration parsed;
  SATISFY_STRING_OR_THROW(TemporalDurationString, DurationRecord, parsed)

  // 3. Let sign, years, months, weeks, days, hours, fHours, minutes, fMinutes,
  // seconds, and fSeconds be the parts of isoString produced respectively by
  // the Sign, DurationYears, DurationMonths, DurationWeeks, DurationDays,
  // DurationWholeHours, DurationHoursFraction, DurationWholeMinutes,
  // DurationMinutesFraction, DurationWholeSeconds, and DurationSecondsFraction
  // productions, or undefined if not present.
  // 4. If sign is the code unit 0x002D (HYPHEN-MINUS) or 0x2212 (MINUS SIGN),
  // then a. Let factor be −1.
  // 5. Else,
  // a. Let factor be 1.
  int32_t factor = (parsed.sign == "-") ? -1 : 1;
  // 6. Set years to ? ToIntegerOrInfinity(years) × factor.
  result.years = StringToInt(parsed.years) * factor;
  // 7. Set months to ? ToIntegerOrInfinity(months) × factor.
  result.months = StringToInt(parsed.months) * factor;
  // 8. Set weeks to ? ToIntegerOrInfinity(weeks) × factor.
  result.weeks = StringToInt(parsed.weeks) * factor;
  // 9. Set days to ? ToIntegerOrInfinity(days) × factor.
  result.days = StringToInt(parsed.days) * factor;
  // 10. Set hours to ? ToIntegerOrInfinity(hours) × factor.
  result.hours = StringToInt(parsed.whole_hours) * factor;
  // 11. Set minutes to ? ToIntegerOrInfinity(minutes) × factor.
  result.minutes = StringToInt(parsed.whole_minutes) * factor;
  // 12. Set seconds to ? ToIntegerOrInfinity(seconds) × factor.
  result.seconds = StringToInt(parsed.whole_seconds) * factor;

  // 13. If fSeconds is not undefined, then
  if (!parsed.seconds_fraction.empty()) {
    // See https://github.com/tc39/proposal-temporal/pull/1759
    // a. Let fSecondsDigits be the substring of fSeconds from 1.
    // b. Let fSecondsDigitsExtended be the string-concatenation of
    // fSecondsDigits and *"000000000"*.
    std::string f_seconds_digits_extended =
        parsed.seconds_fraction.substr(1) + "000000000";

    // c. Let milliseconds be the substring of fSecondsDigitsExtended from 0
    // to 3. d. Set milliseconds to ! ToIntegerOrInfinity(milliseconds) ×
    // factor.
    result.milliseconds =
        StringToInt(f_seconds_digits_extended.substr(0, 3)) * factor;

    // e. Let microseconds be the substring of fSecondsDigitsExtended from 3
    // to 6. f. Set microseconds to ! ToIntegerOrInfinity(microseconds) ×
    // factor.
    result.microseconds =
        StringToInt(f_seconds_digits_extended.substr(3, 3)) * factor;

    // g. Let nanoseconds be the substring of fSecondsDigitsExtended_ from 6
    // to 9. h. Set nanoseconds to ! ToIntegerOrInfinity(nanoseconds) × factor.
    result.nanoseconds =
        StringToInt(f_seconds_digits_extended.substr(6, 3)) * factor;
  } else {
    // 14. Else,
    // a. Let milliseconds be 0.
    result.milliseconds = 0;
    // b. Let microseconds be 0.
    result.microseconds = 0;
    // c. Let nanoseconds be 0.
    result.nanoseconds = 0;
  }
  double f_hours = 0.0;
  // 15. If fHours is not undefined, then
  if (!parsed.hours_fraction.empty()) {
    // a. Let fHoursDigits be the substring of fHours from 1.
    std::string f_hours_digits = parsed.hours_fraction.substr(1);
    // b. Let fHoursScale be the length of fHoursDigits.
    size_t f_hours_scale = f_hours_digits.length();
    // c. Set fHours_ to ! ToIntegerOrInfinity(fHoursDigits) × factor / 10 ^
    // fHoursScale.
    f_hours = StringToInt(f_hours_digits) * factor;
    for (size_t i = 0; i < f_hours_scale; i++) {
      f_hours /= 10.0;
    }
    // 16. Else,
    // a. Set fHours to 0.
  }
  double f_minutes = 0.0;
  // 17. If fMinutes is not *undefined*, then
  if (!parsed.minutes_fraction.empty()) {
    // a. Let fMinutesDigits be the substring of fMinutes from 1.
    std::string f_minutes_digits = parsed.minutes_fraction.substr(1);
    // b. Let fMinutesScale be the length of fMinutesDigits.
    size_t f_minutes_scale = f_minutes_digits.length();
    // c. Set fMinutes to ! ToIntegerOrInfinity(fMinutesDigits) × factor / 10 ^
    // fMinutesScale.
    f_minutes = StringToInt(f_minutes_digits) * factor;
    for (size_t i = 0; i < f_minutes_scale; i++) {
      f_minutes /= 10.0;
    }
    // 18. Else,
    // a. Set fMinutes to 0.
  }
  // 19. Let result be ? DurationHandleFractions(fHours, minutes, fMinutes,
  // seconds, milliseconds, microseconds, nanoseconds).

  Maybe<bool> maybe_fraction_result = DurationHandleFraction(
      isolate, f_hours, &(result.minutes), f_minutes, &(result.seconds),
      &result.milliseconds, &result.microseconds, &result.nanoseconds);
  MAYBE_RETURN(maybe_fraction_result, Nothing<DurationRecord>());
  CHECK(maybe_fraction_result.FromJust());

  // 20. Return the new Record { [[Years]]: years, [[Months]]: months,
  // [[Weeks]]: weeks, [[Days]]: days, [[Hours]]: hours, [[Minutes]]:
  // result.[[Minutes]], [[Seconds]]: result.[[Seconds]], [[Milliseconds]]:
  // result.[[Milliseconds]], [[Microseconds]]: result.[[Microseconds]],
  // [[Nanoseconds]]: result.[[Nanoseconds]] }.
  return Just(result);
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
        sign *
        (((hours * 60 + minutes) * 60 + seconds) * 1000000000L + nanoseconds);
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

MaybeHandle<BigInt> ParseTemporalInstant(Isolate* isolate,
                                         Handle<String> iso_string) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  // 1. Assert: Type(isoString) is String.
  // 2. Let result be ? ParseTemporalInstantString(isoString).
  Maybe<InstantRecord> maybe_result =
      ParseTemporalInstantString(isolate, iso_string);
  MAYBE_RETURN(maybe_result, Handle<BigInt>());
  InstantRecord result = maybe_result.FromJust();

  // 3. Let offsetString be result.[[TimeZoneOffsetString]].
  // 4. Assert: offsetString is not undefined.
  CHECK(!result.offset_string.empty());

  // 5. Let utc be ? GetEpochFromISOParts(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]]).
  Handle<BigInt> utc;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, utc,
      GetEpochFromISOParts(isolate, result.year, result.month, result.day,
                           result.hour, result.minute, result.second,
                           result.millisecond, result.microsecond,
                           result.nanosecond),
      BigInt);

  // 6. If utc < −8.64 × 1021 or utc > 8.64 × 1021, then
  if ((BigInt::CompareToNumber(utc, factory->NewNumber(-8.64e21)) ==
       ComparisonResult::kLessThan) ||
      (BigInt::CompareToNumber(utc, factory->NewNumber(8.64e21)) ==
       ComparisonResult::kGreaterThan)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), BigInt);
  }
  // 7. Let offsetNanoseconds be ? ParseTimeZoneOffsetString(offsetString).
  Maybe<int64_t> maybe_offset_nanoseconds = ParseTimeZoneOffsetString(
      isolate, isolate->factory()->NewStringFromAsciiChecked(
                   result.offset_string.c_str()));
  MAYBE_RETURN(maybe_offset_nanoseconds, Handle<BigInt>());
  int64_t offset_nanoseconds = maybe_offset_nanoseconds.FromJust();

  // 8. Return utc − offsetNanoseconds.
  return BigInt::Subtract(isolate, utc,
                          BigInt::FromInt64(isolate, offset_nanoseconds));
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
  // 5. If ! IsBuiltinCalendar(id) is false, then
  if (!temporal::IsBuiltinCalendar(isolate, parsed.calendar_name)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kInvalidCalendar,
                      isolate->factory()->NewStringFromAsciiChecked(
                          parsed.calendar_name.c_str())),
        Nothing<std::string>());
  }
  // 6. Return id.
  return Just(parsed.calendar_name);
}

// #sec-temporal-parsetemporalrelativetostring
Maybe<ZonedDateTimeRecord> ParseTemporalRelativeToString(
    Isolate* isolate, Handle<String> iso_string) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(isoString) is String.
  // 2. If isoString does not satisfy the syntax of a TemporalRelativeToString
  // (see 13.33), then
  ParsedResult parsed;
  SATISFY_STRING_OR_THROW(TemporalRelativeToString, ZonedDateTimeRecord, parsed)

  // 3. Let result be ! ParseISODateTime(isoString).
  Maybe<DateTimeRecord> maybe_result = ParseISODateTime(isolate, parsed);

  MAYBE_RETURN(maybe_result, Nothing<ZonedDateTimeRecord>());
  DateTimeRecord result = maybe_result.FromJust();
  // 4. If isoString satisfies the syntax of a TemporalZonedDateTimeString
  // (see 13.33), then
  //
  ParsedResult parsed_time_zone;
  bool valid_temporal_zoned_date_time_string = false;
  SATISFY_STRING(TemporalZonedDateTimeString, parsed_time_zone,
                 valid_temporal_zoned_date_time_string);

  ZonedDateTimeRecord record;
  if (valid_temporal_zoned_date_time_string) {
    // a. Let timeZoneResult be ! ParseTemporalTimeZoneString(isoString).
    Maybe<TimeZoneRecord> maybe_time_zone_result =
        ParseTemporalTimeZoneString(isolate, iso_string);
    MAYBE_RETURN(maybe_time_zone_result, Nothing<ZonedDateTimeRecord>());
    TimeZoneRecord time_zone_result = maybe_time_zone_result.FromJust();

    // b. Let z be timeZoneResult.[[Z]].
    record.time_zone_z = time_zone_result.z;
    // c. Let offset be timeZoneResult.[[Offset]].
    record.offset_string = time_zone_result.offset_string;
    // d. Let timeZone be timeZoneResult.[[Name]].
    record.time_zone_name = time_zone_result.name;
    // 5. Else,
  } else {
    // a. Let z be false.
    record.time_zone_z = false;
    // b. Let offset be undefined.
    record.offset_string = "";
    // c. Let timeZone be undefined.
    record.time_zone_name = "";
  }
  // 6. Return the Record { [[Year]]: result.[[Year]], [[Month]]:
  // result.[[Month]], [[Day]]: result.[[Day]], [[Hour]]: result.[[Hour]],
  // [[Minute]]: result.[[Minute]], [[Second]]: result.[[Second]],
  // [[Millisecond]]: result.[[Millisecond]], [[Microsecond]]:
  // result.[[Microsecond]], [[Nanosecond]]: result.[[Nanosecond]],
  // [[Calendar]]: result.[[Calendar]], [[TimeZoneZ]]: z, [[TimeZoneOffset]]:
  // offset, [[TimeZoneIANAName]]: timeZone }.
  record.year = result.year;
  record.month = result.month;
  record.day = result.day;
  record.hour = result.hour;
  record.minute = result.minute;
  record.second = result.second;
  record.millisecond = result.millisecond;
  record.microsecond = result.microsecond;
  record.nanosecond = result.nanosecond;
  record.calendar = result.calendar;

  return Just(record);
}

// #sec-temporal-defaultmergefields
MaybeHandle<JSReceiver> DefaultMergeFields(
    Isolate* isolate, Handle<JSReceiver> fields,
    Handle<JSReceiver> additional_fields) {
  Factory* factory = isolate->factory();
  // 1. Let merged be ! OrdinaryObjectCreate(%Object.prototype%).
  Handle<JSObject> merged =
      isolate->factory()->NewJSObject(isolate->object_function());

  // 2. Let originalKeys be ? EnumerableOwnPropertyNames(fields, key).
  Handle<FixedArray> original_keys;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, original_keys,
                             JSReceiver::OwnPropertyKeys(fields), JSReceiver);
  // 3. For each element nextKey of originalKeys, do
  for (int i = 0; i < original_keys->length(); i++) {
    // a. If nextKey is not "month" or "monthCode", then
    if (original_keys->get(i).IsString()) {
      Handle<String> next_key =
          Handle<String>::cast(Handle<Object>(original_keys->get(i), isolate));
      if (!(factory->month_string()->Equals(*(next_key)) ||
            factory->monthCode_string()->Equals(*next_key))) {
        PropertyDescriptor desc;
        Maybe<bool> maybe_desc = JSReceiver::GetOwnPropertyDescriptor(
            isolate, fields, next_key, &desc);
        MAYBE_RETURN(maybe_desc, Handle<JSReceiver>());
        if (maybe_desc.FromJust() && desc.enumerable()) {
          // i. Let propValue be ? Get(fields, nextKey).
          Handle<Object> prop_value;
          ASSIGN_RETURN_ON_EXCEPTION(
              isolate, prop_value,
              JSReceiver::GetProperty(isolate, fields, next_key), JSReceiver);
          // ii. If propValue is not undefined, then
          if (!prop_value->IsUndefined()) {
            // 1. Perform ! CreateDataPropertyOrThrow(merged, nextKey,
            // propValue).
            CHECK(JSReceiver::CreateDataProperty(isolate, merged, next_key,
                                                 prop_value, Just(kDontThrow))
                      .FromJust());
          }
        }
      }
    }
  }
  // 4. Let newKeys be ? EnumerableOwnPropertyNames(additionalFields, key).
  Handle<FixedArray> new_keys;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, new_keys,
                             JSReceiver::OwnPropertyKeys(additional_fields),
                             JSReceiver);
  bool new_keys_has_month = false;
  bool new_keys_has_month_code = false;
  // 5. For each element nextKey of newKeys, do
  for (int i = 0; i < new_keys->length(); i++) {
    if (new_keys->get(i).IsString()) {
      Handle<String> next_key =
          Handle<String>::cast(Handle<Object>(new_keys->get(i), isolate));
      PropertyDescriptor desc;
      Maybe<bool> maybe_desc = JSReceiver::GetOwnPropertyDescriptor(
          isolate, additional_fields, next_key, &desc);
      MAYBE_RETURN(maybe_desc, Handle<JSReceiver>());
      if (maybe_desc.FromJust() && desc.enumerable()) {
        // a. Let propValue be ? Get(additionalFields, nextKey).
        Handle<Object> prop_value;
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, prop_value,
            JSReceiver::GetProperty(isolate, additional_fields, next_key),
            JSReceiver);
        // b. If propValue is not undefined, then
        if (!prop_value->IsUndefined()) {
          // 1. Perform ! CreateDataPropertyOrThrow(merged, nextKey, propValue).
          CHECK(JSReceiver::CreateDataProperty(isolate, merged, next_key,
                                               prop_value, Just(kDontThrow))
                    .FromJust());
        }
      }
      new_keys_has_month |= factory->month_string()->Equals(*next_key);
      new_keys_has_month_code |= factory->monthCode_string()->Equals(*next_key);
    }
  }
  // 6. If newKeys does not contain either "month" or "monthCode", then
  if (!(new_keys_has_month || new_keys_has_month_code)) {
    // a. Let month be ? Get(fields, "month").
    Handle<Object> month;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, month,
        JSReceiver::GetProperty(isolate, fields, factory->month_string()),
        JSReceiver);
    // b. If month is not undefined, then
    if (!month->IsUndefined()) {
      // i. Perform ! CreateDataPropertyOrThrow(merged, "month", month).
      CHECK(JSReceiver::CreateDataProperty(isolate, merged,
                                           factory->month_string(), month,
                                           Just(kDontThrow))
                .FromJust());
    }
    // c. Let monthCode be ? Get(fields, "monthCode").
    Handle<Object> month_code;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, month_code,
        JSReceiver::GetProperty(isolate, fields, factory->monthCode_string()),
        JSReceiver);
    // d. If monthCode is not undefined, then
    if (!month_code->IsUndefined()) {
      // i. Perform ! CreateDataPropertyOrThrow(merged, "monthCode", monthCode).
      CHECK(JSReceiver::CreateDataProperty(isolate, merged,
                                           factory->monthCode_string(),
                                           month_code, Just(kDontThrow))
                .FromJust());
    }
  }
  // 7. Return merged.
  return merged;
}

#define INVOKE_CALENDAR(ret, name, R)                                  \
  /* 1. Assert: Type(calendar) is Object. */                           \
  CHECK(calendar->IsObject());                                         \
  /* 2. Let result be ? Invoke(calendar, #name, « dateLike »). */    \
  Handle<Object> function;                                             \
  ASSIGN_RETURN_ON_EXCEPTION(                                          \
      isolate, function,                                               \
      Object::GetProperty(isolate, calendar,                           \
                          isolate->factory()->name##_string()),        \
      R);                                                              \
  if (!function->IsCallable()) {                                       \
    THROW_NEW_ERROR(isolate,                                           \
                    NewTypeError(MessageTemplate::kCalledNonCallable,  \
                                 isolate->factory()->name##_string()), \
                    R);                                                \
  }                                                                    \
  Handle<Object> argv[] = {date_like};                                 \
  Handle<Object> ret;                                                  \
  ASSIGN_RETURN_ON_EXCEPTION(                                          \
      isolate, ret, Execution::Call(isolate, function, calendar, 1, argv), R);

#define INVOKE_CALENDAR_AND_THROW_UNDEFINED(ret, name, R)               \
  INVOKE_CALENDAR(ret, name, R)                                         \
  /* 3. If result is undefined, throw a RangeError exception. */        \
  if (ret->IsUndefined()) {                                             \
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), R); \
  }

#define CALENDAR_ABSTRACT_OPERATION_INT_ACTION(Name, property, Action)       \
  MaybeHandle<Object> Calendar##Name(Isolate* isolate,                       \
                                     Handle<JSReceiver> calendar,            \
                                     Handle<JSReceiver> date_like) {         \
    /* 1. Assert: Type(calendar) is Object.   */                             \
    /* 2. Let result be ? Invoke(calendar, property, « dateLike »). */     \
    /* 3. If result is undefined, throw a RangeError exception. */           \
    INVOKE_CALENDAR_AND_THROW_UNDEFINED(result, property, Object)            \
    /* 4. Return ? ToIntegerOrInfinity(result). */                           \
    ASSIGN_RETURN_ON_EXCEPTION(                                              \
        isolate, result, ToIntegerThrowOnInfinity(isolate, result), Object); \
    return Handle<Smi>(Smi::FromInt(result->Number()), isolate);             \
  }

// #sec-temporal-calendaryear
CALENDAR_ABSTRACT_OPERATION_INT_ACTION(Year, year, ToIntegerThrowOnInfinity)
// #sec-temporal-calendarmonth
CALENDAR_ABSTRACT_OPERATION_INT_ACTION(Month, month, ToPositiveInteger)
// #sec-temporal-calendarday
CALENDAR_ABSTRACT_OPERATION_INT_ACTION(Day, day, ToPositiveInteger)
// #sec-temporal-calendarmonthcode
MaybeHandle<Object> CalendarMonthCode(Isolate* isolate,
                                      Handle<JSReceiver> calendar,
                                      Handle<JSReceiver> date_like) {
  // 1. Assert: Type(calendar) is Object.
  // 2. Let result be ? Invoke(calendar, monthCode , « dateLike »).
  // 3. If result is undefined, throw a RangeError exception.
  INVOKE_CALENDAR_AND_THROW_UNDEFINED(result, monthCode, Object)
  // 4. Return ? ToString(result).
  return Object::ToString(isolate, result);
}

#ifdef V8_INTL_SUPPORT
// #sec-temporal-calendarerayear
MaybeHandle<Object> CalendarEraYear(Isolate* isolate,
                                    Handle<JSReceiver> calendar,
                                    Handle<JSReceiver> date_like) {
  // 1. Assert: Type(calendar) is Object.
  // 2. Let result be ? Invoke(calendar, eraYear , « dateLike »).
  INVOKE_CALENDAR(result, eraYear, Object)
  // 3. If result is not undefined, set result to ? ToIntegerOrInfinity(result).
  if (!result->IsUndefined()) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, result, ToIntegerThrowOnInfinity(isolate, result), Object);
  }
  // 4. Return result.
  return result;
}

// #sec-temporal-calendarera
MaybeHandle<Object> CalendarEra(Isolate* isolate, Handle<JSReceiver> calendar,
                                Handle<JSReceiver> date_like) {
  // 1. Assert: Type(calendar) is Object.
  // 2. Let result be ? Invoke(calendar, era , « dateLike »).
  INVOKE_CALENDAR(result, era, Object)
  // 3. If result is not undefined, set result to ? ToString(result).
  if (!result->IsUndefined()) {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                               Object::ToString(isolate, result), Object);
  }
  // 4. Return result.
  return result;
}

#endif  //  V8_INTL_SUPPORT

#define CALENDAR_ABSTRACT_OPERATION(R, Name, property)                         \
  MaybeHandle<R> Calendar##Name(Isolate* isolate, Handle<JSReceiver> calendar, \
                                Handle<JSReceiver> date_like) {                \
    INVOKE_CALENDAR(result, property, Object)                                  \
    return result;                                                             \
  }

// #sec-temporal-calendardayofweek
CALENDAR_ABSTRACT_OPERATION(Object, DayOfWeek, dayOfWeek)
// #sec-temporal-calendardayofyear
CALENDAR_ABSTRACT_OPERATION(Object, DayOfYear, dayOfYear)
// #sec-temporal-calendarweekofyear
CALENDAR_ABSTRACT_OPERATION(Object, WeekOfYear, weekOfYear)
// #sec-temporal-calendardaysinweek
CALENDAR_ABSTRACT_OPERATION(Object, DaysInWeek, daysInWeek)
// #sec-temporal-calendardaysinmonth
CALENDAR_ABSTRACT_OPERATION(Object, DaysInMonth, daysInMonth)
// #sec-temporal-calendardaysinyear
CALENDAR_ABSTRACT_OPERATION(Object, DaysInYear, daysInYear)
// #sec-temporal-calendarmonthsinyear
CALENDAR_ABSTRACT_OPERATION(Object, MonthsInYear, monthsInYear)
// #sec-temporal-calendarinleapyear
CALENDAR_ABSTRACT_OPERATION(Object, InLeapYear, inLeapYear)

MaybeHandle<FixedArray> CalendarFields(Isolate* isolate,
                                       Handle<JSReceiver> calendar,
                                       Handle<FixedArray> field_names) {
  // 1. Let fields be ? GetMethod(calendar, "fields").
  Handle<Object> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      Object::GetMethod(calendar, isolate->factory()->fields_string()),
      FixedArray);
  // 2. Let fieldsArray be ! CreateArrayFromList(fieldNames).
  Handle<Object> fields_array =
      isolate->factory()->NewJSArrayWithElements(field_names);
  // 3. If fields is not undefined, then
  if (!fields->IsUndefined()) {
    // a. Set fieldsArray to ? Call(fields, calendar, « fieldsArray »).
    Handle<Object> argv[] = {fields_array};
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, fields_array,
        Execution::Call(isolate, fields, calendar, 1, argv), FixedArray);
  }
  // 4. Return ? IterableToListOfType(fieldsArray, « String »).
  Handle<Object> argv[] = {fields_array};
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields_array,
      Execution::CallBuiltin(isolate,
                             isolate->string_fixed_array_from_iterable(),
                             fields_array, 1, argv),
      FixedArray);
  CHECK(fields_array->IsFixedArray());
  return Handle<FixedArray>::cast(fields_array);
}

MaybeHandle<JSReceiver> CalendarMergeFields(
    Isolate* isolate, Handle<JSReceiver> calendar, Handle<JSReceiver> fields,
    Handle<JSReceiver> additional_fields) {
  // 1. Let mergeFields be ? GetMethod(calendar, "mergeFields").
  Handle<Object> merge_fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, merge_fields,
      Object::GetMethod(calendar, isolate->factory()->mergeFields_string()),
      JSReceiver);
  // 2. If mergeFields is undefined, then
  if (merge_fields->IsUndefined()) {
    // a. Return ? DefaultMergeFields(fields, additionalFields).
    return DefaultMergeFields(isolate, fields, additional_fields);
  }
  // 3. Return ? Call(mergeFields, calendar, « fields, additionalFields »).
  Handle<Object> argv[] = {fields, additional_fields};
  Handle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      Execution::Call(isolate, merge_fields, calendar, 2, argv), JSReceiver);
  // 4. If Type(result) is not Object, throw a TypeError exception.
  if (!result->IsJSReceiver()) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalDuration);
  }
  Handle<JSReceiver> obj = Handle<JSReceiver>::cast(result);
  return obj;
}

MaybeHandle<JSTemporalPlainDate> CalendarDateAdd(Isolate* isolate,
                                                 Handle<JSReceiver> calendar,
                                                 Handle<Object> date,
                                                 Handle<Object> duration,
                                                 Handle<Object> options) {
  return CalendarDateAdd(isolate, calendar, date, duration, options,
                         isolate->factory()->undefined_value());
}

MaybeHandle<JSTemporalPlainDate> CalendarDateAdd(
    Isolate* isolate, Handle<JSReceiver> calendar, Handle<Object> date,
    Handle<Object> duration, Handle<Object> options, Handle<Object> date_add) {
  // 1. Assert: Type(calendar) is Object.
  // 2. If dateAdd is not present, set dateAdd to ? GetMethod(calendar,
  // "dateAdd").
  if (date_add->IsUndefined()) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, date_add,
        Object::GetMethod(calendar, isolate->factory()->dateAdd_string()),
        JSTemporalPlainDate);
  }
  // 3. Let addedDate be ? Call(dateAdd, calendar, « date, duration, options »).
  Handle<Object> argv[] = {date, duration, options};
  Handle<Object> added_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, added_date,
      Execution::Call(isolate, date_add, calendar, 3, argv),
      JSTemporalPlainDate);
  // 4. Perform ? RequireInternalSlot(addedDate, [[InitializedTemporalDate]]).
  if (!added_date->IsJSTemporalPlainDate()) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainDate);
  }
  // 5. Return addedDate.
  return Handle<JSTemporalPlainDate>::cast(added_date);
}

MaybeHandle<JSTemporalDuration> CalendarDateUntil(Isolate* isolate,
                                                  Handle<JSReceiver> calendar,
                                                  Handle<Object> one,
                                                  Handle<Object> two,
                                                  Handle<Object> options) {
  return CalendarDateUntil(isolate, calendar, one, two, options,
                           isolate->factory()->undefined_value());
}

MaybeHandle<JSTemporalDuration> CalendarDateUntil(
    Isolate* isolate, Handle<JSReceiver> calendar, Handle<Object> one,
    Handle<Object> two, Handle<Object> options, Handle<Object> date_until) {
  // 1. Assert: Type(calendar) is Object.
  // 2. If dateUntil is not present, set dateUntil to ? GetMethod(calendar,
  // "dateUntil").
  if (date_until->IsUndefined()) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, date_until,
        Object::GetMethod(calendar, isolate->factory()->dateUntil_string()),
        JSTemporalDuration);
  }
  // 3. Let duration be ? Call(dateUntil, calendar, « one, two, options »).
  Handle<Object> argv[] = {one, two, options};
  Handle<Object> duration;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, duration,
      Execution::Call(isolate, date_until, calendar, 3, argv),
      JSTemporalDuration);
  // 4. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  if (!duration->IsJSTemporalDuration()) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalDuration);
  }
  // 5. Return duration.
  return Handle<JSTemporalDuration>::cast(duration);
}

// 1. Assert: Type(calendar) is Object.
// 2. Assert: Type(fields) is Object.
// a. Assert: Type(options) is Object.
// 3. Let date be ? Invoke(calendar, "dateFromFields", « fields, options »).
// 4. Perform ? RequireInternalSlot(date, [[InitializedTemporalDate]]).
// 5. Return date.
#define IMPL_FROM_FIELDS_ABSTRACT_OPERATION(Name, name)                     \
  MaybeHandle<JSTemporalPlain##Name> Name##FromFields(                      \
      Isolate* isolate, Handle<JSReceiver> calendar,                        \
      Handle<JSReceiver> fields, Handle<Object> options) {                  \
    Handle<Object> function;                                                \
    ASSIGN_RETURN_ON_EXCEPTION(                                             \
        isolate, function,                                                  \
        Object::GetProperty(isolate, calendar,                              \
                            isolate->factory()->name##FromFields_string()), \
        JSTemporalPlain##Name);                                             \
    if (!function->IsCallable()) {                                          \
      THROW_NEW_ERROR(                                                      \
          isolate,                                                          \
          NewTypeError(MessageTemplate::kCalledNonCallable,                 \
                       isolate->factory()->name##FromFields_string()),      \
          JSTemporalPlain##Name);                                           \
    }                                                                       \
    Handle<Object> argv[] = {fields, options};                              \
    Handle<Object> result;                                                  \
    ASSIGN_RETURN_ON_EXCEPTION(                                             \
        isolate, result,                                                    \
        Execution::Call(isolate, function, calendar, 2, argv),              \
        JSTemporalPlain##Name);                                             \
    if (!result->IsJSTemporalPlain##Name()) {                               \
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),        \
                      JSTemporalPlain##Name);                               \
    }                                                                       \
    return Handle<JSTemporalPlain##Name>::cast(result);                     \
  }

IMPL_FROM_FIELDS_ABSTRACT_OPERATION(Date, date)
IMPL_FROM_FIELDS_ABSTRACT_OPERATION(YearMonth, yearMonth)
IMPL_FROM_FIELDS_ABSTRACT_OPERATION(MonthDay, monthDay)
#undef IMPL_FROM_FIELDS_ABSTRACT_OPERATION

MaybeHandle<Oddball> CalendarEquals(Isolate* isolate, Handle<JSReceiver> one,
                                    Handle<JSReceiver> two) {
  // 1. If one and two are the same Object value, return true.
  Maybe<bool> maybe_equals = Object::Equals(isolate, one, two);
  MAYBE_RETURN(maybe_equals, Handle<Oddball>());
  if (maybe_equals.FromJust()) {
    return isolate->factory()->true_value();
  }
  // 2. Let calendarOne be ? ToString(one).
  Handle<String> calendar_one;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar_one,
                             Object::ToString(isolate, one), Oddball);
  // 3. Let calendarTwo be ? ToString(two).
  Handle<String> calendar_two;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar_two,
                             Object::ToString(isolate, two), Oddball);
  // 4. If calendarOne is calendarTwo, return true.
  if (String::Equals(isolate, calendar_one, calendar_two)) {
    return isolate->factory()->true_value();
  }
  // 5. Return false.
  return isolate->factory()->false_value();
}

// #sec-temporal-consolidatecalendars
MaybeHandle<JSReceiver> ConsolidateCalendars(Isolate* isolate,
                                             Handle<JSReceiver> one,
                                             Handle<JSReceiver> two) {
  Factory* factory = isolate->factory();
  // 1. If one and two are the same Object value, return two.
  Maybe<bool> maybe_equals = Object::Equals(isolate, one, two);
  MAYBE_RETURN(maybe_equals, Handle<JSReceiver>());
  if (maybe_equals.FromJust()) {
    return two;
  }
  // 2. Let calendarOne be ? ToString(one).
  Handle<String> calendar_one;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar_one,
                             Object::ToString(isolate, one), JSReceiver);
  // 3. Let calendarTwo be ? ToString(two).
  Handle<String> calendar_two;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar_two,
                             Object::ToString(isolate, two), JSReceiver);
  // 4. If calendarOne is calendarTwo, return two.
  if (String::Equals(isolate, calendar_one, calendar_two)) {
    return two;
  }
  // 5. If calendarOne is "iso8601", return two.
  if (String::Equals(isolate, calendar_one, factory->iso8601_string())) {
    return two;
  }
  // 6. If calendarTwo is "iso8601", return one.
  if (String::Equals(isolate, calendar_two, factory->iso8601_string())) {
    return one;
  }
  // 7. Throw a RangeError exception.
  THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), JSReceiver);
}

Maybe<int64_t> GetOffsetNanosecondsFor(Isolate* isolate,
                                       Handle<JSReceiver> time_zone,
                                       Handle<Object> instant) {
  TEMPORAL_ENTER_FUNC
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

#ifdef V8_INTL_SUPPORT
namespace {
V8_DECLARE_ONCE(initialize_calendar_map);
const std::map<std::string, int32_t>* k_calendar_id_indecies;
const std::vector<std::string>* k_calendar_ids;
void InitializeCalendarMap() {
  std::map<std::string, int32_t>* calendar_id_indecies =
      new std::map<std::string, int32_t>();
  std::vector<std::string>* calendar_ids = new std::vector<std::string>();
  icu::Locale locale("und");
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::StringEnumeration> enumeration(
      icu::Calendar::getKeywordValuesForLocale("ca", locale, false, status));
  calendar_ids->push_back("iso8601");
  calendar_id_indecies->insert({"iso8601", 0});
  int32_t i = 1;
  for (const char* item = enumeration->next(nullptr, status);
       U_SUCCESS(status) && item != nullptr;
       item = enumeration->next(nullptr, status)) {
    if (strcmp(item, "iso8601") != 0) {
      const char* type = uloc_toUnicodeLocaleType("ca", item);
      calendar_ids->push_back(type);
      calendar_id_indecies->insert({type, i++});
    }
  }
  k_calendar_id_indecies = calendar_id_indecies;
  k_calendar_ids = calendar_ids;
}

void EnsureInitializeCalendarMap() {
  base::CallOnce(&initialize_calendar_map, &InitializeCalendarMap);
}
}  // namespace

// #sec-temporal-isbuiltincalendar
bool IsBuiltinCalendar(Isolate* isolate, const std::string& id) {
  EnsureInitializeCalendarMap();
  CHECK(k_calendar_id_indecies != nullptr);
  return k_calendar_id_indecies->find(id) != k_calendar_id_indecies->end();
}

bool IsBuiltinCalendar(Isolate* isolate, Handle<String> id) {
  return IsBuiltinCalendar(isolate, id->ToCString().get());
}

Handle<String> CalendarIdentifier(Isolate* isolate, int32_t index) {
  EnsureInitializeCalendarMap();
  CHECK(k_calendar_ids != nullptr);
  CHECK_LT(index, k_calendar_ids->size());
  return isolate->factory()->NewStringFromAsciiChecked(
      (*k_calendar_ids)[index].c_str());
}

int32_t CalendarIndex(Isolate* isolate, Handle<String> id) {
  EnsureInitializeCalendarMap();
  return k_calendar_id_indecies->find(id->ToCString().get())->second;
}

#else
Handle<String> CalendarIdentifier(Isolate* isolate, int32_t index) {
  CHECK_EQ(index, 0);
  return isolate->factory()->iso8601_string();
}

// #sec-temporal-isbuiltincalendar
bool IsBuiltinCalendar(Isolate* isolate, const std::string& id) {
  // 1. If id is not "iso8601", return false.
  // 2. Return true
  return id == "iso8601";
}
bool IsBuiltinCalendar(Isolate* isolate, Handle<String> id) {
  // 1. If id is not "iso8601", return false.
  // 2. Return true
  return isolate->factory()->iso8601_string()->Equals(*id);
}

int32_t CalendarIndex(Isolate* isolate, Handle<String> id) { return 0; }
#endif  // V8_INTL_SUPPORT

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

// #sec-temporal-timezoneequals
Maybe<bool> TimeZoneEquals(Isolate* isolate, Handle<Object> one,
                           Handle<Object> two) {
  // 1. If one and two are the same Object value, return true.
  Maybe<bool> maybe_obj_equals = Object::Equals(isolate, one, two);
  MAYBE_RETURN(maybe_obj_equals, Nothing<bool>());
  if (maybe_obj_equals.FromJust()) {
    return Just(true);
  }
  // 2. Let timeZoneOne be ? ToString(one).
  Handle<String> time_zone_one;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_zone_one, Object::ToString(isolate, one), Nothing<bool>());
  // 3. Let timeZoneTwo be ? ToString(two).
  Handle<String> time_zone_two;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_zone_two, Object::ToString(isolate, two), Nothing<bool>());
  // 4. If timeZoneOne is timeZoneTwo, return true.
  if (String::Equals(isolate, time_zone_one, time_zone_two)) {
    return Just(true);
  }
  // 5. Return false.
  return Just(false);
}

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

namespace {
// The common part of PrepareTemporalFields and PreparePartialTemporalFields
// #sec-temporal-preparetemporalfields
// #sec-temporal-preparepartialtemporalfields
V8_WARN_UNUSED_RESULT MaybeHandle<JSObject> PrepareTemporalFieldsOrPartial(
    Isolate* isolate, Handle<JSReceiver> fields, Handle<FixedArray> field_names,
    bool require_day, bool require_time_zone, bool partial) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  // 1. Assert: Type(fields) is Object.
  // 2. Let result be ! OrdinaryObjectCreate(%Object.prototype%).
  Handle<JSObject> result =
      isolate->factory()->NewJSObject(isolate->object_function());
  // 3. For each value property of fieldNames, do
  int length = field_names->length();
  bool any = false;
  for (int i = 0; i < length; i++) {
    Handle<Object> property_obj = Handle<Object>(field_names->get(i), isolate);
    CHECK(property_obj->IsString());
    Handle<String> property;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, property, Object::ToString(isolate, property_obj), JSObject);

    // a. Let value be ? Get(fields, property).
    Handle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, value, Object::GetPropertyOrElement(isolate, fields, property),
        JSObject);

    // b. If value is undefined, then
    if (value->IsUndefined()) {
      // If it is not partial, then we try to check required fields.
      if (partial) continue;
      // i. If requiredFields contains property, then
      if ((require_day && property->Equals(*(factory->day_string()))) ||
          (require_time_zone &&
           property->Equals(*(factory->timeZone_string())))) {
        // 1. Throw a TypeError exception.
        THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                        JSObject);
      }
      // ii. Else,
      // 1. If property is in the Property column of Table 13, then
      // a. Set value to the corresponding Default value of the same row.
      if (property->Equals(*(factory->hour_string())) ||
          property->Equals(*(factory->minute_string())) ||
          property->Equals(*(factory->second_string())) ||
          property->Equals(*(factory->millisecond_string())) ||
          property->Equals(*(factory->microsecond_string())) ||
          property->Equals(*(factory->nanosecond_string()))) {
        value = Handle<Object>(Smi::zero(), isolate);
      }
    } else {
      any = partial;
      // c. Else,
      // i. If property is in the Property column of Table 13 and there is a
      // Conversion value in the same row, then
      // 1. Let Conversion represent the abstract operation named by the
      // Conversion value of the same row.
      // 2. Set value to ? Conversion(value).
      if (property->Equals(*(factory->month_string())) ||
          property->Equals(*(factory->day_string()))) {
        ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                                   ToPositiveInteger(isolate, value), JSObject);
      } else if (property->Equals(*(factory->year_string())) ||
                 property->Equals(*(factory->hour_string())) ||
                 property->Equals(*(factory->minute_string())) ||
                 property->Equals(*(factory->second_string())) ||
                 property->Equals(*(factory->millisecond_string())) ||
                 property->Equals(*(factory->microsecond_string())) ||
                 property->Equals(*(factory->nanosecond_string())) ||
                 property->Equals(*(factory->eraYear_string()))) {
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, value, ToIntegerThrowOnInfinity(isolate, value), JSObject);
      } else if (property->Equals(*(factory->monthCode_string())) ||
                 property->Equals(*(factory->offset_string())) ||
                 property->Equals(*(factory->era_string()))) {
        ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                                   Object::ToString(isolate, value), JSObject);
      }
    }

    // d. Perform ! CreateDataPropertyOrThrow(result, property, value).
    CHECK(JSReceiver::CreateDataProperty(isolate, result, property, value,
                                         Just(kThrowOnError))
              .FromJust());
  }
  // 5. If any is false, then
  if (partial && !any) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(), JSObject);
  }
  // 4. Return result.
  return result;
}

}  // namespace

// #sec-temporal-preparetemporalfields
MaybeHandle<JSObject> PrepareTemporalFields(Isolate* isolate,
                                            Handle<JSReceiver> fields,
                                            Handle<Object> field_names,
                                            bool require_day,
                                            bool require_time_zone) {
  TEMPORAL_ENTER_FUNC

  Handle<FixedArray> names;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, names,
      Object::CreateListFromArrayLike(isolate, field_names,
                                      ElementTypes::kStringAndSymbol),
      JSObject);
  return PrepareTemporalFieldsOrPartial(isolate, fields, names, require_day,
                                        require_time_zone, false);
}

V8_WARN_UNUSED_RESULT MaybeHandle<JSObject> PrepareTemporalFields(
    Isolate* isolate, Handle<JSReceiver> fields, Handle<FixedArray> field_names,
    bool require_day, bool require_time_zone) {
  TEMPORAL_ENTER_FUNC

  return PrepareTemporalFieldsOrPartial(isolate, fields, field_names,
                                        require_day, require_time_zone, false);
}

// #sec-temporal-preparepartialtemporalfields
MaybeHandle<JSObject> PreparePartialTemporalFields(
    Isolate* isolate, Handle<JSReceiver> fields,
    Handle<FixedArray> field_names) {
  TEMPORAL_ENTER_FUNC

  return PrepareTemporalFieldsOrPartial(isolate, fields, field_names, false,
                                        false, true);
}

// #sec-temporal-totemporaldurationrecord
Maybe<DurationRecord> ToTemporalDurationRecord(
    Isolate* isolate, Handle<JSReceiver> temporal_duration_like,
    const char* method) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(temporalDurationLike) is Object.
  // 2. If temporalDurationLike has an [[InitializedTemporalDuration]] internal
  // slot, then
  DurationRecord record;
  if (temporal_duration_like->IsJSTemporalDuration()) {
    // a. Return the Record { [[Years]]: temporalDurationLike.[[Years]],
    // [[Months]]: temporalDurationLike.[[Months]], [[Weeks]]:
    // temporalDurationLike.[[Weeks]], [[Days]]: temporalDurationLike.[[Days]],
    // [[Hours]]: temporalDurationLike.[[Hours]], [[Minutes]]:
    // temporalDurationLike.[[Minutes]], [[Seconds]]:
    // temporalDurationLike.[[Seconds]], [[Milliseconds]]:
    // temporalDurationLike.[[Milliseconds]], [[Microseconds]]:
    // temporalDurationLike.[[Microseconds]], [[Nanoseconds]]:
    // temporalDurationLike.[[Nanoseconds]] }.
    Handle<JSTemporalDuration> duration =
        Handle<JSTemporalDuration>::cast(temporal_duration_like);
    record.years = duration->years().Number();
    record.months = duration->months().Number();
    record.weeks = duration->weeks().Number();
    record.days = duration->days().Number();
    record.hours = duration->hours().Number();
    record.minutes = duration->minutes().Number();
    record.seconds = duration->seconds().Number();
    record.milliseconds = duration->milliseconds().Number();
    record.microseconds = duration->microseconds().Number();
    record.nanoseconds = duration->nanoseconds().Number();
    return Just(record);
  }
  // 3. Let result be a new Record with all the internal slots given in the
  // Internal Slot column in Table 7.
  // 4. Let any be false.
  bool any = false;
  // 5. For each row of Table 7, except the header row, in table order, do
#define READ_ROW(result, temporal_duration_like, prop)                      \
  {                                                                         \
    Handle<Object> val;                                                     \
    /* a. Let prop be the Property value of the current row. */             \
    /* b. Let val be ? Get(temporalDurationLike, prop). */                  \
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(                                       \
        isolate, val,                                                       \
        Object::GetPropertyOrElement(isolate, temporal_duration_like,       \
                                     isolate->factory()->prop##_string()),  \
        Nothing<DurationRecord>());                                         \
    /* c. If val is undefined, then */                                      \
    if (val->IsUndefined()) {                                               \
      /* i. Set result's internal slot whose name is the Internal Slot      \
       * value of the current row to 0. */                                  \
      result.prop = 0;                                                      \
      /* d. Else, */                                                        \
    } else {                                                                \
      /* i. Set any to true. */                                             \
      any = true;                                                           \
      /* ii. Let val be ? ToNumber(val). */                                 \
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, val,                        \
                                       Object::ToNumber(isolate, val),      \
                                       Nothing<DurationRecord>());          \
      double val_number = val->Number();                                    \
      /* iii. If ! IsIntegerNumber(val) is false, then */                   \
      if (val_number - std::floor(val_number) != 0) {                       \
        /* 1. Throw a RangeError exception. */                              \
        THROW_NEW_ERROR_RETURN_VALUE(isolate,                               \
                                     NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), \
                                     Nothing<DurationRecord>());            \
      }                                                                     \
      /* iv. Set result's internal slot whose name is the Internal Slot     \
         value of the current row to val. */                                \
      result.prop = val_number;                                             \
    }                                                                       \
  }
  READ_ROW(record, temporal_duration_like, days);
  READ_ROW(record, temporal_duration_like, hours);
  READ_ROW(record, temporal_duration_like, microseconds);
  READ_ROW(record, temporal_duration_like, milliseconds);
  READ_ROW(record, temporal_duration_like, minutes);
  READ_ROW(record, temporal_duration_like, months);
  READ_ROW(record, temporal_duration_like, nanoseconds);
  READ_ROW(record, temporal_duration_like, seconds);
  READ_ROW(record, temporal_duration_like, weeks);
  READ_ROW(record, temporal_duration_like, years);
#undef READ_ROW
  // 6. If any is false, then
  if (!any) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                                 Nothing<DurationRecord>());
  }
  // 7. Return result.
  return Just(record);
}

// #sec-temporal-tolimitedtemporalduration
Maybe<DurationRecord> ToLimitedTemporalDuration(
    Isolate* isolate, Handle<Object> temporal_duration_like,
    std::set<Unit> disallowed_fields, const char* method) {
  TEMPORAL_ENTER_FUNC

  DurationRecord duration;
  // 1. If Type(temporalDurationLike) is not Object, then
  if (!temporal_duration_like->IsJSReceiver()) {
    // a. Let str be ? ToString(temporalDurationLike).
    Handle<String> str;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, str, Object::ToString(isolate, temporal_duration_like),
        Nothing<DurationRecord>());
    // b. Let duration be ? ParseTemporalDurationString(str).
    Maybe<DurationRecord> maybe_duration =
        ParseTemporalDurationString(isolate, str);
    MAYBE_RETURN(maybe_duration, Nothing<DurationRecord>());
    duration = maybe_duration.FromJust();
  } else {
    // 2. Else,
    // a. Let duration be ? ToTemporalDurationRecord(temporalDurationLike).
    Maybe<DurationRecord> maybe_duration = ToTemporalDurationRecord(
        isolate, Handle<JSReceiver>::cast(temporal_duration_like), method);
    MAYBE_RETURN(maybe_duration, Nothing<DurationRecord>());
    duration = maybe_duration.FromJust();
  }
  // 3. If ! IsValidDuration(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]]) is false, throw a
  // RangeError exception.
  if (!IsValidDuration(isolate, duration)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                                 Nothing<DurationRecord>());
  }

  // 4. For each row of Table 7, except the header row, in table order, do
#define THROW_IF_DISALLOW(d, name, disallowed)                          \
  if ((d.name != 0) && (disallowed_fields.find(Unit::k##disallowed) !=  \
                        disallowed_fields.end())) {                     \
    THROW_NEW_ERROR_RETURN_VALUE(isolate,                               \
                                 NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), \
                                 Nothing<DurationRecord>());            \
  }
  // a. Let prop be the Property value of the current row.
  // b. Let value be duration's internal slot whose name is the Internal Slot
  // value of the current row. c. If value is not 0 and disallowedFields
  // contains prop, then i. Throw a RangeError exception.

  THROW_IF_DISALLOW(duration, days, Day);
  THROW_IF_DISALLOW(duration, hours, Hour);
  THROW_IF_DISALLOW(duration, microseconds, Microsecond);
  THROW_IF_DISALLOW(duration, milliseconds, Millisecond);
  THROW_IF_DISALLOW(duration, minutes, Minute);
  THROW_IF_DISALLOW(duration, months, Month);
  THROW_IF_DISALLOW(duration, nanoseconds, Nanosecond);
  THROW_IF_DISALLOW(duration, seconds, Second);
  THROW_IF_DISALLOW(duration, weeks, Week);
  THROW_IF_DISALLOW(duration, years, Year);
#undef THROW_IF_DISALLOW

  // 5. Return duration.
  return Just(duration);
}

// #sec-temporal-interprettemporaldatetimefields
Maybe<DateTimeRecord> InterpretTemporalDateTimeFields(
    Isolate* isolate, Handle<JSReceiver> calendar, Handle<JSReceiver> fields,
    Handle<JSReceiver> options, const char* method) {
  TEMPORAL_ENTER_FUNC

  DateTimeRecord result;
  // 1. Let timeResult be ? ToTemporalTimeRecord(fields).
  Maybe<TimeRecord> maybe_time_result =
      ToTemporalTimeRecord(isolate, fields, method);
  MAYBE_RETURN(maybe_time_result, Nothing<DateTimeRecord>());
  TimeRecord time_result = maybe_time_result.FromJust();
  // 2. Let temporalDate be ? DateFromFields(calendar, fields, options).
  Handle<JSTemporalPlainDate> temporal_date;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, temporal_date,
      DateFromFields(isolate, calendar, fields, options),
      Nothing<DateTimeRecord>());
  // 3. Let overflow be ? ToTemporalOverflow(options).
  Maybe<ShowOverflow> maybe_overflow =
      ToTemporalOverflow(isolate, options, method);
  MAYBE_RETURN(maybe_overflow, Nothing<DateTimeRecord>());
  ShowOverflow overflow = maybe_overflow.FromJust();
  // 4. Let timeResult be ? RegulateTime(timeResult.[[Hour]],
  // timeResult.[[Minute]], timeResult.[[Second]], timeResult.[[Millisecond]],
  // timeResult.[[Microsecond]], timeResult.[[Nanosecond]], overflow).
  result.hour = time_result.hour;
  result.minute = time_result.minute;
  result.second = time_result.second;
  result.millisecond = time_result.millisecond;
  result.microsecond = time_result.microsecond;
  result.nanosecond = time_result.nanosecond;
  Maybe<bool> maybe_regulate_time_result = RegulateTime(
      isolate, &result.hour, &result.minute, &result.second,
      &result.millisecond, &result.microsecond, &result.nanosecond, overflow);
  MAYBE_RETURN(maybe_regulate_time_result, Nothing<DateTimeRecord>());
  // 5. Return the new Record { [[Year]]: temporalDate.[[ISOYear]], [[Month]]:
  // temporalDate.[[ISOMonth]], [[Day]]: temporalDate.[[ISODay]], [[Hour]]:
  // timeResult.[[Hour]], [[Minute]]: timeResult.[[Minute]], [[Second]]:
  // timeResult.[[Second]], [[Millisecond]]: timeResult.[[Millisecond]],
  // [[Microsecond]]: timeResult.[[Microsecond]], [[Nanosecond]]:
  // timeResult.[[Nanosecond]] }.
  result.year = temporal_date->iso_year();
  result.month = temporal_date->iso_month();
  result.day = temporal_date->iso_day();
  return Just(result);
}

// #sec-temporal-totemporaltimerecord
Maybe<TimeRecord> ToTemporalTimeRecord(Isolate* isolate,
                                       Handle<JSReceiver> temporal_time_like,
                                       const char* method) {
  TEMPORAL_ENTER_FUNC

  TimeRecord result;
  Factory* factory = isolate->factory();
  // 1. Assert: Type(temporalTimeLike) is Object.
  // 2. Let result be the new Record { [[Hour]]: undefined, [[Minute]]:
  // undefined, [[Second]]: undefined, [[Millisecond]]: undefined,
  // [[Microsecond]]: undefined, [[Nanosecond]]: undefined }.
  // See https://github.com/tc39/proposal-temporal/pull/1862
  // 3. Let _any_ be *false*.
  bool any = false;
  // 4. For each row of Table 3, except the header row, in table order, do
#define GET_AND_SET_FIELD(d, s, name)                                          \
  {                                                                            \
    Handle<Object> value;                                                      \
    /* a. Let property be the Property value of the current row. */            \
    /* b. Let value be ? Get(temporalTimeLike, property). */                   \
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(                                          \
        isolate, value,                                                        \
        Object::GetPropertyOrElement(isolate, s, factory->name##_string()),    \
        Nothing<TimeRecord>());                                                \
    /* c. If value is not undefined, then */                                   \
    if (!value->IsUndefined()) {                                               \
      /* i. Set _any_ to *true*. */                                            \
      any = true;                                                              \
    }                                                                          \
    /* d. Set value to ? ToIntegerThrowOnOInfinity(value). */                  \
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, value,                           \
                                     ToIntegerThrowOnInfinity(isolate, value), \
                                     Nothing<TimeRecord>());                   \
    d.name = value->Number();                                                  \
  }
  // e. Set result's internal slot whose name is the Internal Slot value of the
  // current row to value.
  GET_AND_SET_FIELD(result, temporal_time_like, hour)
  GET_AND_SET_FIELD(result, temporal_time_like, microsecond)
  GET_AND_SET_FIELD(result, temporal_time_like, millisecond)
  GET_AND_SET_FIELD(result, temporal_time_like, minute)
  GET_AND_SET_FIELD(result, temporal_time_like, nanosecond)
  GET_AND_SET_FIELD(result, temporal_time_like, second)

  // 5. If _any_ is *false*, then
  if (!any) {
    // a. Throw a *TypeError* exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                                 Nothing<TimeRecord>());
  }
  // 4. Return result.
  return Just(result);
}

// #sec-temporal-mergelargestunitoption
MaybeHandle<JSObject> MergeLargestUnitOption(Isolate* isolate,
                                             Handle<JSReceiver> options,
                                             Unit largest_unit) {
  TEMPORAL_ENTER_FUNC

  // 1. Let merged be ! OrdinaryObjectCreate(%Object.prototype%).
  Handle<JSObject> merged =
      isolate->factory()->NewJSObject(isolate->object_function());
  // 2. Let keys be ? EnumerableOwnPropertyNames(options, key).
  // 3. For each element nextKey of keys, do
  // a. Let propValue be ? Get(options, nextKey).
  // b. Perform ! CreateDataPropertyOrThrow(merged, nextKey, propValue).
  JSReceiver::SetOrCopyDataProperties(
      isolate, merged, options, PropertiesEnumerationMode::kEnumerationOrder)
      .Check();

  // 4. Perform ! CreateDataPropertyOrThrow(merged, "largestUnit", largestUnit).
  CHECK(JSReceiver::CreateDataProperty(
            isolate, merged, isolate->factory()->largestUnit_string(),
            UnitToString(isolate, largest_unit), Just(kThrowOnError))
            .FromJust());
  // 5. Return merged.
  return merged;
}

Maybe<double> DaysUntil(Isolate* isolate, Handle<Object> earlier,
                        Handle<Object> later, const char* method) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: earlier and later both have [[ISOYear]], [[ISOMonth]], and
  // [[ISODay]] internal slots.

  int32_t earlier_year, earlier_month, earlier_day, later_year, later_month,
      later_day;
#define GET_ISO_YEAR_MONTH_DAY(prefix, T)                          \
  if (prefix->IsJSTemporal##T()) {                                 \
    Handle<JSTemporal##T> t = Handle<JSTemporal##T>::cast(prefix); \
    prefix##_year = t->iso_year();                                 \
    prefix##_month = t->iso_month();                               \
    prefix##_day = t->iso_day();                                   \
    prefix##_done = true;                                          \
  }

#define GET_ISO_YEAR_MONTH_DAY_FROM_TEMPORAL_OBJECT(p) \
  {                                                    \
    bool p##_done = false;                             \
    GET_ISO_YEAR_MONTH_DAY(p, PlainDate)               \
    GET_ISO_YEAR_MONTH_DAY(p, PlainDateTime)           \
    GET_ISO_YEAR_MONTH_DAY(p, PlainYearMonth)          \
    GET_ISO_YEAR_MONTH_DAY(p, PlainMonthDay)           \
    CHECK(p##_done);                                   \
  }

  GET_ISO_YEAR_MONTH_DAY_FROM_TEMPORAL_OBJECT(earlier)
  GET_ISO_YEAR_MONTH_DAY_FROM_TEMPORAL_OBJECT(later)

#undef GET_ISO_YEAR_MONTH_DAY
#undef GET_ISO_YEAR_MONTH_DAY_FROM_TEMPORAL_OBJECT

  // 2. Let difference be ! DifferenceISODate(earlier.[[ISOYear]],
  // earlier.[[ISOMonth]], earlier.[[ISODay]], later.[[ISOYear]],
  // later.[[ISOMonth]], later.[[ISODay]], "day").

  double years, months, weeks, days;
  Maybe<bool> maybe_difference =
      DifferenceISODate(isolate, earlier_year, earlier_month, earlier_day,
                        later_year, later_month, later_day, Unit::kDay, &years,
                        &months, &weeks, &days, method);
  MAYBE_RETURN(maybe_difference, Nothing<double>());

  // 3. Return difference.[[Days]].
  return Just(days);
}

// #sec-temporal-moverelativedate
MaybeHandle<JSTemporalPlainDateTime> MoveRelativeDate(
    Isolate* isolate, Handle<JSReceiver> calendar,
    Handle<JSTemporalPlainDateTime> relative_to,
    Handle<JSTemporalDuration> duration, double* result_days,
    const char* method) {
  TEMPORAL_ENTER_FUNC

  // 1. Let options be ! OrdinaryObjectCreate(null).
  Handle<JSObject> options = isolate->factory()->NewJSObjectWithNullProto();

  // 2. Let later be ? CalendarDateAdd(calendar, relativeTo, duration, options).
  Handle<JSTemporalPlainDate> later;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, later,
      CalendarDateAdd(isolate, calendar, relative_to, duration, options),
      JSTemporalPlainDateTime);

  // 3. Let days be ? DaysUntil(relativeTo, later).
  Maybe<double> maybe_days = DaysUntil(isolate, relative_to, later, method);
  MAYBE_RETURN(maybe_days, Handle<JSTemporalPlainDateTime>());

  *result_days = maybe_days.FromJust();
  // 4. Let dateTime be ? CreateTemporalDateTime(later.[[ISOYear]],
  // later.[[ISOMonth]], later.[[ISODay]], relativeTo.[[ISOHour]],
  // relativeTo.[[ISOMinute]], relativeTo.[[ISOSecond]],
  // relativeTo.[[ISOMillisecond]], relativeTo.[[ISOMicrosecond]],
  // relativeTo.[[ISONanosecond]], relativeTo.[[Calendar]]).
  // 5. Return the Record { [[RelativeTo]]: dateTime, [[Days]]: days }.
  return temporal::CreateTemporalDateTime(
      isolate, later->iso_year(), later->iso_month(), later->iso_day(),
      relative_to->iso_hour(), relative_to->iso_minute(),
      relative_to->iso_second(), relative_to->iso_millisecond(),
      relative_to->iso_microsecond(), relative_to->iso_nanosecond(),
      Handle<JSReceiver>(relative_to->calendar(), isolate));
}

Maybe<bool> RejectTemporalCalendarType(Isolate* isolate,
                                       Handle<Object> object) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(object) is Object.
  CHECK(object->IsJSReceiver());
  // 2. If object has an [[InitializedTemporalDate]],
  // [[InitializedTemporalDateTime]], [[InitializedTemporalMonthDay]],
  // [[InitializedTemporalTime]], [[InitializedTemporalYearMonth]], or
  // [[InitializedTemporalZonedDateTime]] internal slot, then
  if (object->IsJSTemporalPlainDate() || object->IsJSTemporalPlainDateTime() ||
      object->IsJSTemporalPlainMonthDay() || object->IsJSTemporalPlainTime() ||
      object->IsJSTemporalPlainYearMonth() ||
      object->IsJSTemporalZonedDateTime()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                                 Nothing<bool>());
  }
  return Just(true);
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

MaybeHandle<Object> ToPositiveInteger(Isolate* isolate,
                                      Handle<Object> argument) {
  TEMPORAL_ENTER_FUNC

  // 1. Let integer be ? ToInteger(argument).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, argument, ToIntegerThrowOnInfinity(isolate, argument), Object);
  // 2. If integer ≤ 0, then
  if (NumberToInt32(*argument) <= 0) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), Object);
  }
  return argument;
}

// #sec-temporal-toshowcalendaroption
Maybe<ShowCalendar> ToShowCalendarOption(Isolate* isolate,
                                         Handle<JSReceiver> options,
                                         const char* method) {
  // 1. Return ? GetOption(normalizedOptions, "calendarName", « String », «
  // "auto", "always", "never" », "auto").
  return GetStringOption<ShowCalendar>(
      isolate, options, "calendarName", method, {"auto", "always", "never"},
      {ShowCalendar::kAuto, ShowCalendar::kAlways, ShowCalendar::kNever},
      ShowCalendar::kAuto);
}

// #sec-temporal-toshowtimezonenameoption
Maybe<ShowTimeZone> ToShowTimeZoneNameOption(Isolate* isolate,
                                             Handle<JSReceiver> options,
                                             const char* method) {
  return GetStringOption<ShowTimeZone>(
      isolate, options, "timeZoneName", method, {"auto", "never"},
      {ShowTimeZone::kAuto, ShowTimeZone::kNever}, ShowTimeZone::kAuto);
}

// #sec-temporal-totemporaloverflow
Maybe<ShowOverflow> ToTemporalOverflow(Isolate* isolate,
                                       Handle<JSReceiver> options,
                                       const char* method) {
  return GetStringOption<ShowOverflow>(
      isolate, options, "overflow", method, {"constrain", "reject"},
      {ShowOverflow::kConstrain, ShowOverflow::kReject},
      ShowOverflow::kConstrain);
}

// #sec-temporal-totemporaldisambiguation
Maybe<Disambiguation> ToTemporalDisambiguation(Isolate* isolate,
                                               Handle<JSReceiver> options,
                                               const char* method) {
  return GetStringOption<Disambiguation>(
      isolate, options, "disambiguation", method,
      {"compatible", "earlier", "later", "reject"},
      {Disambiguation::kCompatible, Disambiguation::kEarlier,
       Disambiguation::kLater, Disambiguation::kReject},
      Disambiguation::kCompatible);
}

// sec-temporal-totemporalroundingmode
Maybe<RoundingMode> ToTemporalRoundingMode(Isolate* isolate,
                                           Handle<JSReceiver> options,
                                           RoundingMode fallback,
                                           const char* method) {
  return GetStringOption<RoundingMode>(
      isolate, options, "roundingMode", method,
      {"ceil", "floor", "trunc", "halfExpand"},
      {RoundingMode::kCeil, RoundingMode::kFloor, RoundingMode::kTrunc,
       RoundingMode::kHalfExpand},
      fallback);
}

// #sec-temporal-toshowoffsetoption
Maybe<ShowOffset> ToShowOffsetOption(Isolate* isolate,
                                     Handle<JSReceiver> options,
                                     const char* method) {
  return GetStringOption<ShowOffset>(
      isolate, options, "offset", method, {"auto", "never"},
      {ShowOffset::kAuto, ShowOffset::kNever}, ShowOffset::kAuto);
}

// #sec-temporal-totemporaloffset
Maybe<Offset> ToTemporalOffset(Isolate* isolate, Handle<JSReceiver> options,
                               Offset fallback, const char* method) {
  return GetStringOption<Offset>(
      isolate, options, "offset", method, {"prefer", "use", "ignore", "reject"},
      {Offset::kPrefer, Offset::kUse, Offset::kIgnore, Offset::kReject},
      fallback);
}

#define UNIT_STRINGS                                                          \
  "year", "years", "month", "months", "week", "weeks", "day", "days", "hour", \
      "hours", "minute", "minutes", "second", "seconds", "millisecond",       \
      "milliseconds", "microsecond", "microseconds", "nanosecond",            \
      "nanoseconds"

#define UNIT_ENUM                                                    \
  Unit::kYear, Unit::kYear, Unit::kMonth, Unit::kMonth, Unit::kWeek, \
      Unit::kWeek, Unit::kDay, Unit::kDay, Unit::kHour, Unit::kHour, \
      Unit::kMinute, Unit::kMinute, Unit::kSecond, Unit::kSecond,    \
      Unit::kMillisecond, Unit::kMillisecond, Unit::kMicrosecond,    \
      Unit::kMicrosecond, Unit::kNanosecond, Unit::kNanosecond

#define UNIT_STRINGS_AND_ENUM \
  {UNIT_STRINGS}, { UNIT_ENUM }
#define UNIT_STRINGS_AND_ENUM_WITH_AUTO \
  {"auto", UNIT_STRINGS}, { Unit::kAuto, UNIT_ENUM }

// #sec-temporal-tolargesttemporalunit
Maybe<Unit> ToLargestTemporalUnit(Isolate* isolate,
                                  Handle<JSReceiver> normalized_options,
                                  std::set<Unit> disallowed_units,
                                  Unit fallback, Unit auto_value,
                                  const char* method) {
  // 1. Assert: disallowedUnits does not contain fallback or "auto".
  CHECK_EQ(disallowed_units.find(fallback), disallowed_units.end());
  CHECK_EQ(disallowed_units.find(Unit::kAuto), disallowed_units.end());
  // 2. Assert: If autoValue is present, fallback is "auto", and disallowedUnits
  // does not contain autoValue.
  CHECK(auto_value == Unit::kNotPresent || fallback == Unit::kAuto);
  CHECK(auto_value == Unit::kNotPresent ||
        disallowed_units.find(auto_value) == disallowed_units.end());
  // 3. Let largestUnit be ? GetOption(normalizedOptions, "largestUnit", «
  // String », « "auto", "year", "years", "month", "months", "week", "weeks",
  // "day", "days", "hour", "hours", "minute", "minutes", "second", "seconds",
  // "millisecond", "milliseconds", "microsecond", "microseconds", "nanosecond",
  // "nanoseconds" », fallback).
  Maybe<Unit> maybe_largest_unit =
      GetStringOption<Unit>(isolate, normalized_options, "largestUnit", method,
                            UNIT_STRINGS_AND_ENUM_WITH_AUTO, fallback);
  MAYBE_RETURN(maybe_largest_unit, Nothing<Unit>());
  // 4. If largestUnit is "auto" and autoValue is present, then
  if (maybe_largest_unit.FromJust() == Unit::kAuto &&
      auto_value != Unit::kNotPresent) {
    // a. Return autoValue.
    return Just(auto_value);
  }
  // 5. If largestUnit is in the Plural column of Table 12, then
  // a. Set largestUnit to the corresponding Singular value of the same row.
  // 6. If disallowedUnits contains largestUnit, then
  if (disallowed_units.find(maybe_largest_unit.FromJust()) !=
      disallowed_units.end()) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kInvalidUnit,
                      isolate->factory()->NewStringFromAsciiChecked(method),
                      isolate->factory()->largestUnit_string()),
        Nothing<Unit>());
  }
  // 7. Return largestUnit.
  return maybe_largest_unit;
}

// #sec-temporal-tolargesttemporalunit
Maybe<Unit> ToSmallestTemporalUnit(Isolate* isolate,
                                   Handle<JSReceiver> normalized_options,
                                   std::set<Unit> disallowed_units,
                                   Unit fallback, const char* method) {
  // 1. Assert: disallowedUnits does not contain fallback.
  CHECK_EQ(disallowed_units.find(fallback), disallowed_units.end());
  // 2. Let smallestUnit be ? GetOption(normalizedOptions, "smallestUnit", «
  // String », « "year", "years", "month", "months", "week", "weeks", "day",
  // "days", "hour", "hours", "minute", "minutes", "second", "seconds",
  // "millisecond", "milliseconds", "microsecond", "microseconds", "nanosecond",
  // "nanoseconds" », fallback).
  Maybe<Unit> maybe_smallest_unit =
      GetStringOption<Unit>(isolate, normalized_options, "smallestUnit", method,
                            UNIT_STRINGS_AND_ENUM, fallback);
  MAYBE_RETURN(maybe_smallest_unit, Nothing<Unit>());
  // 3. If smallestUnit is in the Plural column of Table 12, then
  // a. Set smallestUnit to the corresponding Singular value of the same row.
  // 4. If disallowedUnits contains smallestUnit, then
  if (disallowed_units.find(maybe_smallest_unit.FromJust()) !=
      disallowed_units.end()) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kInvalidUnit,
                      isolate->factory()->NewStringFromAsciiChecked(method),
                      isolate->factory()->smallestUnit_string()),
        Nothing<Unit>());
  }
  // 5. Return smallestUnit.
  return maybe_smallest_unit;
}

Unit LargerOfTwoTemporalUnits(Isolate* isolate, Unit u1, Unit u2) {
  // 1. If either u1 or u2 is "year", return "year".
  if (u1 == Unit::kYear || u2 == Unit::kYear) return Unit::kYear;
  // 2. If either u1 or u2 is "month", return "month".
  if (u1 == Unit::kMonth || u2 == Unit::kMonth) return Unit::kMonth;
  // 3. If either u1 or u2 is "week", return "week".
  if (u1 == Unit::kWeek || u2 == Unit::kWeek) return Unit::kWeek;
  // 4. If either u1 or u2 is "day", return "day".
  if (u1 == Unit::kDay || u2 == Unit::kDay) return Unit::kDay;
  // 5. If either u1 or u2 is "hour", return "hour".
  if (u1 == Unit::kHour || u2 == Unit::kHour) return Unit::kHour;
  // 6. If either u1 or u2 is "minute", return "minute".
  if (u1 == Unit::kMinute || u2 == Unit::kMinute) return Unit::kMinute;
  // 7. If either u1 or u2 is "second", return "second".
  if (u1 == Unit::kSecond || u2 == Unit::kSecond) return Unit::kSecond;
  // 8. If either u1 or u2 is "millisecond", return "millisecond".
  if (u1 == Unit::kMillisecond || u2 == Unit::kMillisecond)
    return Unit::kMillisecond;
  // 9. If either u1 or u2 is "microsecond", return "microsecond".
  if (u1 == Unit::kMicrosecond || u2 == Unit::kMicrosecond)
    return Unit::kMicrosecond;
  // 10. Return "nanosecond".
  return Unit::kNanosecond;
}

Unit DefaultTemporalLargestUnit(Isolate* isolate, const DurationRecord& dur) {
  // 1. If years is not zero, return "year".
  if (dur.years != 0) return Unit::kYear;
  // 2. If months is not zero, return "month".
  if (dur.months != 0) return Unit::kMonth;
  // 3. If weeks is not zero, return "week".
  if (dur.weeks != 0) return Unit::kWeek;
  // 4. If days is not zero, return "day".
  if (dur.days != 0) return Unit::kDay;
  // 5dur.. If hours is not zero, return "hour".
  if (dur.hours != 0) return Unit::kHour;
  // 6. If minutes is not zero, return "minute".
  if (dur.minutes != 0) return Unit::kMinute;
  // 7. If seconds is not zero, return "second".
  if (dur.seconds != 0) return Unit::kSecond;
  // 8. If milliseconds is not zero, return "millisecond".
  if (dur.milliseconds != 0) return Unit::kMillisecond;
  // 9. If microseconds is not zero, return "microsecond".
  if (dur.microseconds != 0) return Unit::kMicrosecond;
  // 10. Return "nanosecond".
  return Unit::kNanosecond;
}

// #sec-temporal-tolargesttemporalunit
Maybe<Unit> ToTemporalDurationTotalUnit(Isolate* isolate,
                                        Handle<JSReceiver> normalized_options,
                                        const char* method) {
  // 1. Let unit be ? GetOption(normalizedOptions, "unit", « String », « "year",
  // "years", "month", "months", "week", "weeks", "day", "days", "hour",
  // "hours", "minute", "minutes", "second", "seconds", "millisecond",
  // "milliseconds", "microsecond", "microseconds", "nanosecond", "nanoseconds"
  // », undefined).
  Maybe<Unit> maybe_unit =
      GetStringOption<Unit>(isolate, normalized_options, "unit", method,
                            UNIT_STRINGS_AND_ENUM, Unit::kNotPresent);
  MAYBE_RETURN(maybe_unit, Nothing<Unit>());
  // 2. If unit is undefined, then
  if (maybe_unit.FromJust() == Unit::kNotPresent) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kInvalidUnit,
                      isolate->factory()->NewStringFromAsciiChecked(method),
                      isolate->factory()->unit_string()),
        Nothing<Unit>());
  }
  // 3. If unit is in the Plural column of Table 12, then
  // a. Set unit to the corresponding Singular value of the same row.
  // 4. Return unit.
  return maybe_unit;
}

// #sec-temporal-validatetemporalunitrange
Maybe<bool> ValidateTemporalUnitRange(Isolate* isolate, Unit largest_unit,
                                      Unit smallest_unit, const char* method) {
#define THROW()                                                            \
  THROW_NEW_ERROR_RETURN_VALUE(                                            \
      isolate,                                                             \
      NewRangeError(MessageTemplate::kInvalidUnit,                         \
                    isolate->factory()->NewStringFromAsciiChecked(method), \
                    isolate->factory()->largestUnit_string()),             \
      Nothing<bool>());

  switch (smallest_unit) {
    // 1. If smallestUnit is "year" and largestUnit is not "year", then
    case Unit::kYear:
      if (largest_unit != Unit::kYear) {
        // a. Throw a RangeError exception.
        THROW();
      }
      return Just(true);
    // 2. If smallestUnit is "month" and largestUnit is not "year" or "month",
    // then
    case Unit::kMonth:
      if (!(largest_unit == Unit::kYear || largest_unit == Unit::kMonth)) {
        // a. Throw a RangeError exception.
        THROW();
      }
      return Just(true);
    // 3. If smallestUnit is "week" and largestUnit is not one of "year",
    // "month", or "week", then
    case Unit::kWeek:
      if (!(largest_unit == Unit::kYear || largest_unit == Unit::kMonth ||
            largest_unit == Unit::kWeek)) {
        // a. Throw a RangeError exception.
        THROW();
      }
      return Just(true);
    // 4. If smallestUnit is "day" and largestUnit is not one of "year",
    // "month", "week", or "day", then
    case Unit::kDay:
      if (!(largest_unit == Unit::kYear || largest_unit == Unit::kMonth ||
            largest_unit == Unit::kWeek || largest_unit == Unit::kDay)) {
        // a. Throw a RangeError exception.
        THROW();
      }
      return Just(true);
    // 5. If smallestUnit is "hour" and largestUnit is not one of "year",
    // "month", "week", "day", or "hour", then
    case Unit::kHour:
      if (!(largest_unit == Unit::kYear || largest_unit == Unit::kMonth ||
            largest_unit == Unit::kWeek || largest_unit == Unit::kDay ||
            largest_unit == Unit::kHour)) {
        // a. Throw a RangeError exception.
        THROW();
      }
      return Just(true);
    // 6. If smallestUnit is "minute" and largestUnit is "second",
    // "millisecond", "microsecond", or "nanosecond", then
    case Unit::kMinute:
      if (largest_unit == Unit::kSecond || largest_unit == Unit::kMillisecond ||
          largest_unit == Unit::kMicrosecond ||
          largest_unit == Unit::kNanosecond) {
        // a. Throw a RangeError exception.
        THROW();
      }
      return Just(true);
    // 7. If smallestUnit is "second" and largestUnit is "millisecond",
    // "microsecond", or "nanosecond", then
    case Unit::kSecond:
      if (largest_unit == Unit::kMillisecond ||
          largest_unit == Unit::kMicrosecond ||
          largest_unit == Unit::kNanosecond) {
        // a. Throw a RangeError exception.
        THROW();
      }
      return Just(true);
    // 8. If smallestUnit is "millisecond" and largestUnit is "microsecond" or
    // "nanosecond", then
    case Unit::kMillisecond:
      if (largest_unit == Unit::kMicrosecond ||
          largest_unit == Unit::kNanosecond) {
        // a. Throw a RangeError exception.
        THROW();
      }
      return Just(true);
    // 9. If smallestUnit is "microsecond" and largestUnit is "nanosecond", then
    case Unit::kMicrosecond:
      if (largest_unit == Unit::kNanosecond) {
        // a. Throw a RangeError exception.
        THROW();
      }
#undef THROW
      return Just(true);
    default:
      return Just(true);
  }
}

Handle<String> ShowOverflowToString(Isolate* isolate, ShowOverflow overflow) {
  switch (overflow) {
    case ShowOverflow::kConstrain:
      return ReadOnlyRoots(isolate).constrain_string_handle();
    case ShowOverflow::kReject:
      return ReadOnlyRoots(isolate).reject_string_handle();
    default:
      UNREACHABLE();
  }
}

Handle<String> UnitToString(Isolate* isolate, Unit unit) {
  switch (unit) {
    case Unit::kYear:
      return ReadOnlyRoots(isolate).year_string_handle();
    case Unit::kMonth:
      return ReadOnlyRoots(isolate).month_string_handle();
    case Unit::kWeek:
      return ReadOnlyRoots(isolate).week_string_handle();
    case Unit::kDay:
      return ReadOnlyRoots(isolate).day_string_handle();
    case Unit::kHour:
      return ReadOnlyRoots(isolate).hour_string_handle();
    case Unit::kMinute:
      return ReadOnlyRoots(isolate).minute_string_handle();
    case Unit::kSecond:
      return ReadOnlyRoots(isolate).second_string_handle();
    case Unit::kMillisecond:
      return ReadOnlyRoots(isolate).millisecond_string_handle();
    case Unit::kMicrosecond:
      return ReadOnlyRoots(isolate).microsecond_string_handle();
    case Unit::kNanosecond:
      return ReadOnlyRoots(isolate).nanosecond_string_handle();
    default:
      UNREACHABLE();
  }
}

// GetOption wihle the types is << Number, String >> and values is empty.
MaybeHandle<Object> GetOption_NumberOrString(Isolate* isolate,
                                             Handle<JSReceiver> options,
                                             Handle<String> property,
                                             Handle<Object> fallback,
                                             const char* method) {
  // 1. Assert: Type(options) is Object.
  // 2. Let value be ? GetOption(options, property, « Number, String », empty,
  // fallback).
  Handle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, value, Object::GetPropertyOrElement(isolate, options, property),
      Object);
  // 4. If value is undefined, return fallback.
  if (value->IsUndefined()) {
    return fallback;
  }
  // 5. If types contains Type(value), then
  // a. Let type be Type(value).
  // 6. Else,
  // a. Let type be the last element of types.
  // 7. If type is Boolean, then
  // a. Set value to ! ToBoolean(value).
  // 8. Else if type is Number, then
  if (value->IsNumber()) {
    // a. Set value to ? ToNumber(value).
    ASSIGN_RETURN_ON_EXCEPTION(isolate, value, Object::ToNumber(isolate, value),
                               Object);
    // b. If value is NaN, throw a RangeError exception.
    if (value->IsNaN()) {
      THROW_NEW_ERROR(
          isolate,
          NewRangeError(MessageTemplate::kPropertyValueOutOfRange, property),
          Object);
    }
    return value;
    // 9. Else,
  } else {
    // a. Set value to ? ToString(value).
    ASSIGN_RETURN_ON_EXCEPTION(isolate, value, Object::ToString(isolate, value),
                               Object);
    return value;
  }
}

// In #sec-temporal-tosecondsstringprecision
// 13.16 ToSecondsStringPrecision ( normalizedOptions )
// 8. Let digits be ? GetStringOrNumberOption(normalizedOptions,
// "fractionalSecondDigits", « "auto" », 0, 9, "auto").
Maybe<Precision> GetFractionalSecondDigits(Isolate* isolate,
                                           Handle<JSReceiver> options,
                                           const char* method) {
  // 2. Let value be ? GetOption(options, property, « Number, String », empty,
  // fallback).
  Handle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value,
      GetOption_NumberOrString(
          isolate, options, isolate->factory()->fractionalSecondDigits_string(),
          isolate->factory()->auto_string(), method),
      Nothing<Precision>());

  // 3. If Type(value) is Number, then
  if (value->IsNumber()) {
    // a. If value < minimum or value > maximum, throw a RangeError exception.
    double value_num = value->Number();
    if (value_num < 0 || value_num > 9) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                        isolate->factory()->fractionalSecondDigits_string()),
          Nothing<Precision>());
    }
    // b. Return floor(ℝ(value)).
    int32_t v = std::floor(value_num);
    return Just(static_cast<Precision>(v));
  } else {
    // 4. Assert: Type(value) is String.
    CHECK(value->IsString());
    // 5. If stringValues does not contain value, throw a RangeError exception.
    Handle<String> string_value;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, string_value,
                                     Object::ToString(isolate, value),
                                     Nothing<Precision>());
    if (std::strcmp(string_value->ToCString().get(), "auto") != 0) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                        isolate->factory()->fractionalSecondDigits_string()),
          Nothing<Precision>());
    }
    // 6. Return value.
    return Just(Precision::kAuto);
  }
}

// #sec-temporal-tosecondsstringprecision
Maybe<bool> ToSecondsStringPrecision(Isolate* isolate,
                                     Handle<JSReceiver> normalized_options,
                                     Precision* precision, double* increment,
                                     Unit* unit, const char* method) {
  // 1. Let smallestUnit be ? ToSmallestTemporalUnit(normalizedOptions, «
  // "year", "month", "week", "day", "hour" », undefined).
  Maybe<Unit> maybe_smallest_unit = ToSmallestTemporalUnit(
      isolate, normalized_options,
      std::set<Unit>(
          {Unit::kYear, Unit::kMonth, Unit::kWeek, Unit::kDay, Unit::kHour}),
      Unit::kNotPresent, method);
  MAYBE_RETURN(maybe_smallest_unit, Nothing<bool>());
  Unit smallest_unit = maybe_smallest_unit.FromJust();

  switch (smallest_unit) {
    // 2. If smallestUnit is "minute", then
    case Unit::kMinute:
      // a. Return the new Record { [[Precision]]: "minute", [[Unit]]: "minute",
      // [[Increment]]: 1 }.
      *precision = Precision::kMinute;
      *unit = Unit::kMinute;
      *increment = 1;
      return Just(true);
    // 3. If smallestUnit is "second", then
    case Unit::kSecond:
      // a. Return the new Record { [[Precision]]: 0, [[Unit]]: "second",
      // [[Increment]]: 1 }.
      *precision = Precision::k0;
      *unit = Unit::kSecond;
      *increment = 1;
      return Just(true);
    // 4. If smallestUnit is "millisecond", then
    case Unit::kMillisecond:
      // a. Return the new Record { [[Precision]]: 3, [[Unit]]: "millisecond",
      // [[Increment]]: 1 }.
      *precision = Precision::k3;
      *unit = Unit::kMillisecond;
      *increment = 1;
      return Just(true);
    // 5. If smallestUnit is "microsecond", then
    case Unit::kMicrosecond:
      // a. Return the new Record { [[Precision]]: 6, [[Unit]]: "microsecond",
      // [[Increment]]: 1 }.
      *precision = Precision::k6;
      *unit = Unit::kMicrosecond;
      *increment = 1;
      return Just(true);
    // 6. If smallestUnit is "nanosecond", then
    case Unit::kNanosecond:
      // a. Return the new Record { [[Precision]]: 9, [[Unit]]: "nanosecond",
      // [[Increment]]: 1 }.
      *precision = Precision::k9;
      *unit = Unit::kNanosecond;
      *increment = 1;
      return Just(true);
    default:
      break;
  }
  // 7. Assert: smallestUnit is undefined.
  CHECK(smallest_unit == Unit::kNotPresent);
  // 8. Let digits be ? GetStringOrNumberOption(normalizedOptions,
  // "fractionalSecondDigits", « "auto" », 0, 9, "auto").
  Maybe<Precision> maybe_digits =
      GetFractionalSecondDigits(isolate, normalized_options, method);
  MAYBE_RETURN(maybe_digits, Nothing<bool>());
  *precision = maybe_digits.FromJust();

  switch (*precision) {
    // 9. If digits is "auto", then
    case Precision::kAuto:
      // a. Return the new Record { [[Precision]]: "auto", [[Unit]]:
      // "nanosecond", [[Increment]]: 1 }.
      *unit = Unit::kNanosecond;
      *increment = 1;
      return Just(true);
      // 10. If digits is 0, then
    case Precision::k0:
      // a. Return the new Record { [[Precision]]: 0, [[Unit]]: "second",
      // [[Increment]]: 1 }.
      *unit = Unit::kSecond;
      *increment = 1;
      return Just(true);
    // 11. If digits is 1, 2, or 3, then
    // a. Return the new Record { [[Precision]]: digits, [[Unit]]:
    // "millisecond", [[Increment]]: 103 − digits }.
    case Precision::k1:
      *unit = Unit::kMillisecond;
      *increment = 100;
      return Just(true);
    case Precision::k2:
      *unit = Unit::kMillisecond;
      *increment = 10;
      return Just(true);
    case Precision::k3:
      *unit = Unit::kMillisecond;
      *increment = 1;
      return Just(true);
      // 12. If digits is 4, 5, or 6, then
      // a. Return the new Record { [[Precision]]: digits, [[Unit]]:
      // "microsecond", [[Increment]]: 106 − digits }.
    case Precision::k4:
      *unit = Unit::kMicrosecond;
      *increment = 100;
      return Just(true);
    case Precision::k5:
      *unit = Unit::kMicrosecond;
      *increment = 10;
      return Just(true);
    case Precision::k6:
      *unit = Unit::kMicrosecond;
      *increment = 1;
      return Just(true);
      // 13. Assert: digits is 7, 8, or 9.
      // 14. Return the new Record { [[Precision]]: digits, [[Unit]]:
      // "nanosecond", [[Increment]]: 109 − digits }.
    case Precision::k7:
      *unit = Unit::kNanosecond;
      *increment = 100;
      return Just(true);
    case Precision::k8:
      *unit = Unit::kNanosecond;
      *increment = 10;
      return Just(true);
    case Precision::k9:
      *unit = Unit::kNanosecond;
      *increment = 1;
      return Just(true);
    default:
      UNREACHABLE();
  }
}

// #sec-temporal-maximumtemporaldurationroundingincrement
Maybe<bool> MaximumTemporalDurationRoundingIncrement(Isolate* isolate,
                                                     Unit unit,
                                                     double* maximum) {
  switch (unit) {
    // 1. If unit is "year", "month", "week", or "day", then
    case Unit::kYear:
    case Unit::kMonth:
    case Unit::kWeek:
    case Unit::kDay:
      // a. Return undefined.
      return Just(false);
    // 2. If unit is "hour", then
    case Unit::kHour:
      // a. Return 24.
      *maximum = 24;
      return Just(true);
    // 3. If unit is "minute" or "second", then
    case Unit::kMinute:
    case Unit::kSecond:
      // a. Return 60.
      *maximum = 60;
      return Just(true);
    default:
      // 4. Assert: unit is one of "millisecond", "microsecond", or
      // "nanosecond".
      CHECK(unit == Unit::kMillisecond || unit == Unit::kMicrosecond ||
            unit == Unit::kNanosecond);
      // 5. Return 1000.
      *maximum = 1000;
      return Just(true);
  }
}

Maybe<int> ToTemporalRoundingIncrement(Isolate* isolate,
                                       Handle<JSReceiver> normalized_options,
                                       int dividend, bool dividend_is_defined,
                                       bool inclusive, const char* method) {
  int maximum;
  // 1. If dividend is undefined, then
  if (!dividend_is_defined) {
    // a. Let maximum be +∞.
    maximum = INT_MAX;
    // 2. Else if inclusive is true, then
  } else if (inclusive) {
    // a. Let maximum be dividend.
    maximum = dividend;
    // 3. Else if dividend is more than 1, then
  } else if (dividend > 1) {
    // a. Let maximum be dividend − 1.
    maximum = dividend - 1;
    // 4. Else,
  } else {
    // a. Let maximum be 1.
    maximum = 1;
  }
  // 5. Let increment be ? GetOption(normalizedOptions, "roundingIncrement", «
  // Number », empty, 1).
  // 6. If increment < 1 or increment > maximum, throw a RangeError exception.
  Maybe<int> maybe_increment = GetNumberOption(
      isolate, normalized_options,
      isolate->factory()->roundingIncrement_string(), 1, maximum, 1);
  MAYBE_RETURN(maybe_increment, Nothing<int>());
  // 7. Set increment to floor(ℝ(increment)).
  int increment = maybe_increment.FromJust();

  // 8. If dividend is not undefined and dividend modulo increment is not zero,
  // then
  if ((dividend_is_defined) && ((dividend % increment) != 0)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                                 Nothing<int>());
  }
  // 9. Return increment.
  return Just(increment);
}

Maybe<int> ToTemporalDateTimeRoundingIncrement(
    Isolate* isolate, Handle<JSReceiver> normalized_options, Unit smallest_unit,
    const char* method) {
  double maximum;
  switch (smallest_unit) {
    // 1. If smallestUnit is "day", then
    case Unit::kDay:
      // a. Let maximum be 1.
      maximum = 1;
      break;
    // 2. Else if smallestUnit is "hour", then
    case Unit::kHour:
      // a. Let maximum be 24.
      maximum = 24;
      break;
    // 3. Else if smallestUnit is "minute" or "second", then
    case Unit::kMinute:
    case Unit::kSecond:
      // a. Let maximum be 60.
      maximum = 60;
      break;
    // 4. Else,
    case Unit::kMillisecond:
    case Unit::kMicrosecond:
    case Unit::kNanosecond:
      // a. Assert: smallestUnit is "millisecond", "microsecond", or
      // "nanosecond". b. Let maximum be 1000.
      maximum = 1000;
      break;
    default:
      UNREACHABLE();
  }
  // 5. Return ? ToTemporalRoundingIncrement(normalizedOptions, maximum, false).
  return ToTemporalRoundingIncrement(isolate, normalized_options, maximum, true,
                                     false, method);
}

// #sec-temporal-negatetemporalroundingmode
RoundingMode NegateTemporalRoundingMode(Isolate* isolate,
                                        RoundingMode rounding_mode) {
  // 1. If roundingMode is "ceil", return "floor".
  if (rounding_mode == RoundingMode::kCeil) return RoundingMode::kFloor;
  // 2. If roundingMode is "floor", return "ceil".
  if (rounding_mode == RoundingMode::kFloor) return RoundingMode::kCeil;
  // 3. Return roundingMode.
  return rounding_mode;
}

// #sec-temporal-regulateisodate
Maybe<bool> RegulateISODate(Isolate* isolate, int32_t* year, int32_t* month,
                            int32_t* day, ShowOverflow overflow) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year, month, and day are integers.
  // 2. Assert: overflow is either "constrain" or "reject".
  switch (overflow) {
    // 3. If overflow is "reject", then
    case ShowOverflow::kReject:
      // a. If ! IsValidISODate(year, month, day) is false, throw a RangeError
      // exception.
      if (!IsValidISODate(isolate, *year, *month, *day)) {
        THROW_NEW_ERROR_RETURN_VALUE(
            isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), Nothing<bool>());
      }
      // b. Return the Record { [[Year]]: year, [[Month]]: month, [[Day]]: day
      // }.
      return Just(true);
    // 4. If overflow is "constrain", then
    case ShowOverflow::kConstrain:
      // a. Set month to ! ConstrainToRange(month, 1, 12).
      *month = std::max(std::min(*month, 12), 1);
      // b. Set day to ! ConstrainToRange(day, 1, ! ISODaysInMonth(year,
      // month)).
      *day =
          std::max(std::min(*day, ISODaysInMonth(isolate, *year, *month)), 1);
      // c. Return the Record { [[Year]]: year, [[Month]]: month, [[Day]]: day
      // }.
      return Just(true);
    default:
      UNREACHABLE();
  }
}

Maybe<bool> RegulateTime(Isolate* isolate, int32_t* hour, int32_t* minute,
                         int32_t* second, int32_t* millisecond,
                         int32_t* microsecond, int32_t* nanosecond,
                         ShowOverflow overflow) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: hour, minute, second, millisecond, microsecond and nanosecond
  // are integers.
  // 2. Assert: overflow is either "constrain" or "reject".
  switch (overflow) {
    case ShowOverflow::kConstrain:
      // 3. If overflow is "constrain", then
      // a. Return ! ConstrainTime(hour, minute, second, millisecond,
      // microsecond, nanosecond).
      *hour = std::max(std::min(*hour, 23), 0);
      *minute = std::max(std::min(*minute, 59), 0);
      *second = std::max(std::min(*second, 59), 0);
      *millisecond = std::max(std::min(*millisecond, 999), 0);
      *microsecond = std::max(std::min(*microsecond, 999), 0);
      *nanosecond = std::max(std::min(*nanosecond, 999), 0);
      return Just(true);
    case ShowOverflow::kReject:
      // 4. If overflow is "reject", then
      // a. If ! IsValidTime(hour, minute, second, millisecond, microsecond,
      // nanosecond) is false, throw a RangeError exception.
      if (!IsValidTime(isolate, *hour, *minute, *second, *millisecond,
                       *microsecond, *nanosecond)) {
        THROW_NEW_ERROR_RETURN_VALUE(
            isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), Nothing<bool>());
      }
      // b. Return the new Record { [[Hour]]: hour, [[Minute]]: minute,
      // [[Second]]: second, [[Millisecond]]: millisecond, [[Microsecond]]:
      // microsecond, [[Nanosecond]]: nanosecond }.
      return Just(true);
    default:
      UNREACHABLE();
  }
}

Maybe<bool> DifferenceISODate(Isolate* isolate, int32_t y1, int32_t m1,
                              int32_t d1, int32_t y2, int32_t m2, int32_t d2,
                              Unit largest_unit, double* out_years,
                              double* out_months, double* out_weeks,
                              double* out_days, const char* method) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: largestUnit is one of "year", "month", "week", or "day".
  CHECK(largest_unit == Unit::kYear || largest_unit == Unit::kMonth ||
        largest_unit == Unit::kWeek || largest_unit == Unit::kDay);
  // 2. If largestUnit is "year" or "month", then
  if (largest_unit == Unit::kYear || largest_unit == Unit::kMonth) {
    // a. Let sign be -(! CompareISODate(y1, m1, d1, y2, m2, d2)).
    int32_t sign = -CompareISODate(isolate, y1, m1, d1, y2, m2, d2);
    // b. If sign is 0, return the new Record { [[Years]]: 0, [[Months]]: 0,
    // [[Weeks]]: 0, [[Days]]: 0 }.
    if (sign == 0) {
      *out_years = *out_months = *out_weeks = *out_days = 0;
      return Just(true);
    }
    // c. Let start be the new Record { [[Year]]: y1, [[Month]]: m1, [[Day]]: d1
    // }. d. Let end be the new Record { [[Year]]: y2, [[Month]]: m2, [[Day]]:
    // d2 }. e. Let years be end.[[Year]] − start.[[Year]].
    double years = y2 - y1;
    // f. Let mid be ! AddISODate(y1, m1, d1, years, 0, 0, 0, "constrain").
    int32_t mid_year, mid_month, mid_day;
    Maybe<bool> maybe_mid =
        AddISODate(isolate, y1, m1, d1, years, 0, 0, 0,
                   ShowOverflow::kConstrain, &mid_year, &mid_month, &mid_day);
    MAYBE_RETURN(maybe_mid, Nothing<bool>());

    // g. Let midSign be -(! CompareISODate(mid.[[Year]], mid.[[Month]],
    // mid.[[Day]], y2, m2, d2)).
    int32_t mid_sign =
        -CompareISODate(isolate, mid_year, mid_month, mid_day, y2, m2, d2);

    // h. If midSign is 0, then
    if (mid_sign == 0) {
      // i. If largestUnit is "year", return the new Record { [[Years]]: years,
      // [[Months]]: 0, [[Weeks]]: 0, [[Days]]: 0 }.
      if (largest_unit == Unit::kYear) {
        *out_years = years;
        *out_months = *out_weeks = *out_days = 0;
        return Just(true);
      }
      // ii. Return the new Record { [[Years]]: 0, [[Months]]: years × 12,
      // [[Weeks]]: 0, [[Days]]: 0 }.
      *out_years = *out_weeks = *out_days = 0;
      *out_months = years * 12;
      return Just(true);
    }
    // i. Let months be end.[[Month]] − start.[[Month]].
    double months = m2 - m1;
    // j. If midSign is not equal to sign, then
    if (mid_sign != sign) {
      // i. Set years to years - sign.
      years -= sign;
      // ii. Set months to months + sign × 12.
      months += sign * 12;
    }
    // k. Set mid be ! AddISODate(y1, m1, d1, years, months, 0, 0, "constrain").
    maybe_mid =
        AddISODate(isolate, y1, m1, d1, years, months, 0, 0,
                   ShowOverflow::kConstrain, &mid_year, &mid_month, &mid_day);
    MAYBE_RETURN(maybe_mid, Nothing<bool>());
    // l. Let midSign be -(! CompareISODate(mid.[[Year]], mid.[[Month]],
    // mid.[[Day]], y2, m2, d2)).
    mid_sign =
        -CompareISODate(isolate, mid_year, mid_month, mid_day, y2, m2, d2);
    // m. If midSign is 0, then
    if (mid_sign == 0) {
      // i. If largestUnit is "year", return the new Record { [[Years]]: years,
      // [[Months]]: months, [[Weeks]]: 0, [[Days]]: 0 }.
      if (largest_unit == Unit::kYear) {
        *out_years = years;
        *out_months = months;
        *out_weeks = *out_days = 0;
        return Just(true);
      }
      // ii. Return the new Record { [[Years]]: 0, [[Months]]: months + years ×
      // 12, [[Weeks]]: 0, [[Days]]: 0 }.
      *out_years = *out_weeks = *out_days = 0;
      *out_months = months + years * 12;
      return Just(true);
    }
    // n. If midSign is not equal to sign, then
    if (mid_sign != sign) {
      // i. Set months to months - sign.
      months -= sign;
      // ii. If months is equal to -sign, then
      if (months == -sign) {
        // 1. Set years to years - sign.
        years -= sign;
        // 2. Set months to 11 × sign.
        months = 11 * sign;
      }
      // iii. Set mid be ! AddISODate(y1, m1, d1, years, months, 0, 0,
      // "constrain").
      maybe_mid =
          AddISODate(isolate, y1, m1, d1, years, months, 0, 0,
                     ShowOverflow::kConstrain, &mid_year, &mid_month, &mid_day);
      MAYBE_RETURN(maybe_mid, Nothing<bool>());
      // iv. Let midSign be -(! CompareISODate(mid.[[Year]], mid.[[Month]],
      // mid.[[Day]], y2, m2, d2)).
      mid_sign =
          -CompareISODate(isolate, mid_year, mid_month, mid_day, y2, m2, d2);
    }
    // o. Let days be 0.
    double days = 0;
    // p. If mid.[[Month]] = end.[[Month]], then
    if (mid_month == m2) {
      // i. Assert: mid.[[Year]] = end.[[Year]].
      CHECK_EQ(mid_year, y2);
      // ii. Set days to end.[[Day]] - mid.[[Day]].
      days = d2 - mid_day;
    } else if (sign < 0) {
      // q. Else if sign < 0, set days to -mid.[[Day]] - (!
      // ISODaysInMonth(end.[[Year]], end.[[Month]]) - end.[[Day]]).
      days = -mid_day - (ISODaysInMonth(isolate, y2, m2) - d2);
    } else {
      // r. Else, set days to end.[[Day]] + (! ISODaysInMonth(mid.[[Year]],
      // mid.[[Month]]) - mid.[[Day]]).
      days = d2 + (ISODaysInMonth(isolate, mid_year, mid_month) - mid_day);
    }
    // s. If largestUnit is "month", then
    if (largest_unit == Unit::kMonth) {
      // i. Set months to months + years × 12.
      months += years * 12;
      // ii. Set years to 0.
      years = 0;
    }
    // t. Return the new Record { [[Years]]: years, [[Months]]: months,
    // [[Weeks]]: 0, [[Days]]: days }.
    *out_years = years;
    *out_months = months;
    *out_weeks = 0;
    *out_days = days;
    return Just(true);
  }
  // 3. If largestUnit is "day" or "week", then
  if (largest_unit == Unit::kDay || largest_unit == Unit::kWeek) {
    // a. If ! CompareISODate(y1, m1, d1, y2, m2, d2) < 0, then
    int32_t smaller_y, smaller_m, smaller_d, greater_y, greater_m, greater_d;
    int32_t sign;
    if (CompareISODate(isolate, y1, m1, d1, y2, m2, d2) < 0) {
      // i. Let smaller be the new Record { [[Year]]: y1, [[Month]]: m1,
      // [[Day]]: d1 }.
      smaller_y = y1;
      smaller_m = m1;
      smaller_d = d1;
      // ii. Let greater be the new Record { [[Year]]: y2, [[Month]]: m2,
      // [[Day]]: d2 }.
      greater_y = y2;
      greater_m = m2;
      greater_d = d2;
      // iii. Let sign be 1.
      sign = 1;
    } else {
      // b. Else,
      // i. Let smaller be the new Record { [[Year]]: y2, [[Month]]: m2,
      // [[Day]]: d2 }.
      smaller_y = y2;
      smaller_m = m2;
      smaller_d = d2;
      // ii. Let greater be the new Record { [[Year]]: y1, [[Month]]: m1,
      // [[Day]]: d1 }.
      greater_y = y1;
      greater_m = m1;
      greater_d = d1;
      // iii. Let sign be −1.
      sign = -1;
    }
    // c. Let days be ! ToISODayOfYear(greater.[[Year]], greater.[[Month]],
    // greater.[[Day]]) − ! ToISODayOfYear(smaller.[[Year]], smaller.[[Month]],
    // smaller.[[Day]]).
    double days = ToISODayOfYear(isolate, greater_y, greater_m, greater_d) -
                  ToISODayOfYear(isolate, smaller_y, smaller_m, smaller_d);
    // d. Let year be smaller.[[Year]].
    double year = smaller_y;
    // e. Repeat, while year < greater.[[Year]],
    while (year < greater_y) {
      // i. Set days to days + ! ISODaysInYear(year).
      // ii. Set year to year + 1.
      days += ISODaysInYear(isolate, year++);
    }
    // f. Let weeks be 0.
    double weeks = 0;
    // g. If largestUnit is "week", then
    if (largest_unit == Unit::kWeek) {
      // i. Set weeks to floor(days / 7).
      weeks = FLOOR(days / 7);
      // ii. Set days to days mod 7.
      days = MODULO(days, 7);
    }
    // See TODO(ftang) see PR 1802
    if (weeks != 0) {
      weeks *= sign;
    }
    if (days != 0) {
      days *= sign;
    }
    // h. Return the Record { [[Years]]: 0, [[Months]]: 0, [[Weeks]]: weeks ×
    // sign, [[Days]]: days × sign }.

    *out_years = *out_months = 0;
    *out_weeks = weeks;
    *out_days = days;
    return Just(true);
  }
  UNREACHABLE();
}

// #sec-temporal-addisodate
Maybe<bool> AddISODate(Isolate* isolate, int32_t year, int32_t month,
                       int32_t day, double years, double months, double weeks,
                       double days, ShowOverflow overflow, int32_t* out_year,
                       int32_t* out_month, int32_t* out_day) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year, month, day, years, months, weeks, and days are integers.
  // 2. Assert: overflow is either "constrain" or "reject".
  // 3. Let intermediate be ! BalanceISOYearMonth(year + years, month + months).
  *out_year = year + years;
  *out_month = month + months;
  BalanceISOYearMonth(isolate, out_year, out_month);
  // 4. Let intermediate be ? RegulateISODate(intermediate.[[Year]],
  // intermediate.[[Month]], day, overflow).
  *out_day = day;
  Maybe<bool> maybe_intermediate =
      RegulateISODate(isolate, out_year, out_month, out_day, overflow);
  MAYBE_RETURN(maybe_intermediate, Nothing<bool>());
  // 5. Set days to days + 7 × weeks.
  days += 7 * weeks;
  // 6. Let d be intermediate.[[Day]] + days.
  *out_day += days;
  // 7. Let intermediate be ! BalanceISODate(intermediate.[[Year]],
  // intermediate.[[Month]], d).
  BalanceISODate(isolate, out_year, out_month, out_day);
  // 8. Return ? RegulateISODate(intermediate.[[Year]], intermediate.[[Month]],
  // intermediate.[[Day]], overflow).
  Maybe<bool> ret =
      RegulateISODate(isolate, out_year, out_month, out_day, overflow);
  return ret;
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

// #sec-temporal-adddatetime
Maybe<DateTimeRecordCommon> AddDateTime(
    Isolate* isolate, int32_t year, int32_t month, int32_t day, int32_t hour,
    int32_t minute, int32_t second, int32_t millisecond, int32_t microsecond,
    int32_t nanosecond, Handle<JSReceiver> calendar, const DurationRecord& dur,
    Handle<Object> options) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year, month, day, hour, minute, second, millisecond,
  // microsecond, and nanosecond are integers.
  // 2. Let timeResult be ! AddTime(hour, minute, second, millisecond,
  // microsecond, nanosecond, hours, minutes, seconds, milliseconds,
  // microseconds, nanoseconds).
  DateTimeRecordCommon time_result =
      AddTime(isolate, hour, minute, second, millisecond, microsecond,
              nanosecond, dur.hours, dur.minutes, dur.seconds, dur.milliseconds,
              dur.microseconds, dur.nanoseconds);
  // 3. Let datePart be ? CreateTemporalDate(year, month, day, calendar).
  Handle<JSTemporalPlainDate> date_part;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, date_part,
      CreateTemporalDate(isolate, year, month, day, calendar),
      Nothing<DateTimeRecordCommon>());
  // 4. Let dateDuration be ? CreateTemporalDuration(years, months, weeks, days
  // + timeResult.[[Days]], 0, 0, 0, 0, 0, 0).
  Handle<JSTemporalDuration> date_duration;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, date_duration,
      CreateTemporalDuration(isolate, dur.years, dur.months, dur.weeks,
                             dur.days + time_result.day, 0, 0, 0, 0, 0, 0),
      Nothing<DateTimeRecordCommon>());
  // 5. Let addedDate be ? CalendarDateAdd(calendar, datePart, dateDuration,
  // options).
  Handle<JSTemporalPlainDate> added_date;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, added_date,
      CalendarDateAdd(isolate, calendar, date_part, date_duration, options),
      Nothing<DateTimeRecordCommon>());
  // 6. Return the new Record { [[Year]]: addedDate.[[ISOYear]], [[Month]]:
  // addedDate.[[ISOMonth]], [[Day]]: addedDate.[[ISODay]], [[Hour]]:
  // timeResult.[[Hour]], [[Minute]]: timeResult.[[Minute]], [[Second]]:
  // timeResult.[[Second]], [[Millisecond]]: timeResult.[[Millisecond]],
  // [[Microsecond]]: timeResult.[[Microsecond]], [[Nanosecond]]:
  // timeResult.[[Nanosecond]], }.
  time_result.year = added_date->iso_year();
  time_result.month = added_date->iso_month();
  time_result.day = added_date->iso_day();
  return Just(time_result);
}

Maybe<bool> BalanceDuration(Isolate* isolate, double* days, double* hours,
                            double* minutes, double* seconds,
                            double* milliseconds, double* microseconds,
                            double* nanoseconds, Unit largest_unit) {
  TEMPORAL_ENTER_FUNC

  // 1. If relativeTo is not present, set relativeTo to undefined.
  return BalanceDuration(isolate, days, hours, minutes, seconds, milliseconds,
                         microseconds, nanoseconds, largest_unit,
                         isolate->factory()->undefined_value());
}

Maybe<bool> BalanceDuration(Isolate* isolate, double* days, double* hours,
                            double* minutes, double* seconds,
                            double* milliseconds, double* microseconds,
                            double* nanoseconds, Unit largest_unit,
                            Handle<Object> relative_to_obj) {
  TEMPORAL_ENTER_FUNC

  // 2. If Type(relativeTo) is Object and relativeTo has an
  // [[InitializedTemporalZonedDateTime]] internal slot, then
  if (relative_to_obj->IsJSTemporalZonedDateTime()) {
    Handle<JSTemporalZonedDateTime> relative_to =
        Handle<JSTemporalZonedDateTime>::cast(relative_to_obj);
    // a. Let endNs be ? AddZonedDateTime(relativeTo.[[Nanoseconds]],
    // relativeTo.[[TimeZone]], relativeTo.[[Calendar]], 0, 0, 0, days, hours,
    // minutes, seconds, milliseconds, microseconds, nanoseconds).
    Handle<BigInt> end_ns;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, end_ns,
        AddZonedDateTime(isolate,
                         Handle<BigInt>(relative_to->nanoseconds(), isolate),
                         Handle<JSReceiver>(relative_to->time_zone(), isolate),
                         Handle<JSReceiver>(relative_to->calendar(), isolate),
                         {0, 0, 0, *days, *hours, *minutes, *seconds,
                          *milliseconds, *microseconds, *nanoseconds}),
        Nothing<bool>());
    // b. Set nanoseconds to endNs − relativeTo.[[Nanoseconds]].
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, end_ns,
        BigInt::Subtract(isolate, end_ns,
                         Handle<BigInt>(relative_to->nanoseconds(), isolate)),
        Nothing<bool>());
    *nanoseconds = end_ns->AsInt64();
    // 3. Else,
  } else {
    // a. Set nanoseconds to ℤ(! TotalDurationNanoseconds(days, hours, minutes,
    // seconds, milliseconds, microseconds, nanoseconds, 0)).
    *nanoseconds =
        TotalDurationNanoseconds(isolate, *days, *hours, *minutes, *seconds,
                                 *milliseconds, *microseconds, *nanoseconds, 0);
  }
  // 4. If largestUnit is one of "year", "month", "week", or "day", then
  if (largest_unit == Unit::kYear || largest_unit == Unit::kMonth ||
      largest_unit == Unit::kWeek || largest_unit == Unit::kDay) {
    double result_day_length;
    // a. Let result be ? NanosecondsToDays(nanoseconds, relativeTo).
    Maybe<bool> maybe_result =
        NanosecondsToDays(isolate, *nanoseconds, relative_to_obj, days,
                          nanoseconds, &result_day_length);
    MAYBE_RETURN(maybe_result, Nothing<bool>());
    // b. Set days to result.[[Days]].
    // c. Set nanoseconds to result.[[Nanoseconds]].
    // 5. Else,
  } else {
    // a. Set days to 0.
    *days = 0;
  }
  // 6. Set hours, minutes, seconds, milliseconds, and microseconds to 0.
  *hours = *minutes = *seconds = *milliseconds = *microseconds = 0;
  // 7. Set nanoseconds to ℝ(nanoseconds).
  *nanoseconds = R(*nanoseconds);

  // 8. If nanoseconds < 0, let sign be −1; else, let sign be 1.
  int32_t sign = (*nanoseconds < 0) ? -1 : 1;
  // 9. Set nanoseconds to abs(nanoseconds).
  *nanoseconds = std::abs(*nanoseconds);
  // 10. If largestUnit is "year", "month", "week", "day", or "hour", then
  switch (largest_unit) {
    case Unit::kYear:
    case Unit::kMonth:
    case Unit::kWeek:
    case Unit::kDay:
    case Unit::kHour:
      // a. Set microseconds to floor(nanoseconds / 1000).
      *microseconds = FLOOR(*nanoseconds / 1000);
      // b. Set nanoseconds to nanoseconds modulo 1000.
      *nanoseconds = MODULO(*nanoseconds, 1000);
      // c. Set milliseconds to floor(microseconds / 1000).
      *milliseconds = FLOOR(*microseconds / 1000);
      // d. Set microseconds to microseconds modulo 1000.
      *microseconds = MODULO(*microseconds, 1000);
      // e. Set seconds to floor(milliseconds / 1000).
      *seconds = FLOOR(*milliseconds / 1000);
      // f. Set milliseconds to milliseconds modulo 1000.
      *milliseconds = MODULO(*milliseconds, 1000);
      // g. Set minutes to floor(seconds / 60).
      *minutes = FLOOR(*seconds / 60);
      // h. Set seconds to seconds modulo 60.
      *seconds = MODULO(*seconds, 60);
      // i. Set hours to floor(minutes / 60).
      *hours = FLOOR(*minutes / 60);
      // j. Set minutes to minutes modulo 60.
      *minutes = MODULO(*minutes, 60);
      break;
    // 11. Else if largestUnit is "minute", then
    case Unit::kMinute:
      // a. Set microseconds to floor(nanoseconds / 1000).
      *microseconds = FLOOR(*nanoseconds / 1000);
      // b. Set nanoseconds to nanoseconds modulo 1000.
      *nanoseconds = MODULO(*nanoseconds, 1000);
      // c. Set milliseconds to floor(microseconds / 1000).
      *milliseconds = FLOOR(*microseconds / 1000);
      // d. Set microseconds to microseconds modulo 1000.
      *microseconds = MODULO(*microseconds, 1000);
      // e. Set seconds to floor(milliseconds / 1000).
      *seconds = FLOOR(*milliseconds / 1000);
      // f. Set milliseconds to milliseconds modulo 1000.
      *milliseconds = MODULO(*milliseconds, 1000);
      // g. Set minutes to floor(seconds / 60).
      *minutes = FLOOR(*seconds / 60);
      // h. Set seconds to seconds modulo 60.
      *seconds = MODULO(*seconds, 60);
      break;
    // 12. Else if largestUnit is "second", then
    case Unit::kSecond:
      // a. Set microseconds to floor(nanoseconds / 1000).
      *microseconds = FLOOR(*nanoseconds / 1000);
      // b. Set nanoseconds to nanoseconds modulo 1000.
      *nanoseconds = MODULO(*nanoseconds, 1000);
      // c. Set milliseconds to floor(microseconds / 1000).
      *milliseconds = FLOOR(*microseconds / 1000);
      // d. Set microseconds to microseconds modulo 1000.
      *microseconds = MODULO(*microseconds, 1000);
      // e. Set seconds to floor(milliseconds / 1000).
      *seconds = FLOOR(*milliseconds / 1000);
      // f. Set milliseconds to milliseconds modulo 1000.
      *milliseconds = MODULO(*milliseconds, 1000);
      break;
    // 13. Else if largestUnit is "millisecond", then
    case Unit::kMillisecond:
      // a. Set microseconds to floor(nanoseconds / 1000).
      *microseconds = FLOOR(*nanoseconds / 1000);
      // b. Set nanoseconds to nanoseconds modulo 1000.
      *nanoseconds = MODULO(*nanoseconds, 1000);
      // c. Set milliseconds to floor(microseconds / 1000).
      *milliseconds = FLOOR(*microseconds / 1000);
      // d. Set microseconds to microseconds modulo 1000.
      *microseconds = MODULO(*microseconds, 1000);
      break;
    // 14. Else if largestUnit is "microsecond", then
    case Unit::kMicrosecond:
      // a. Set microseconds to floor(nanoseconds / 1000).
      *microseconds = FLOOR(*nanoseconds / 1000);
      // b. Set nanoseconds to nanoseconds modulo 1000.
      *nanoseconds = MODULO(*nanoseconds, 1000);
      break;
    // 15. Else,
    default:
      // a. Assert: largestUnit is "nanosecond".
      CHECK_EQ(largest_unit, Unit::kNanosecond);
  }
  // 16. Return the new Record { [[Days]]: 𝔽(days), [[Hours]]: 𝔽(hours × sign),
  // [[Minutes]]: 𝔽(minutes × sign), [[Seconds]]: 𝔽(seconds × sign),
  // [[Milliseconds]]: 𝔽(milliseconds × sign), [[Microseconds]]: 𝔽(microseconds
  // × sign), [[Nanoseconds]]: 𝔽(nanoseconds × sign) }.
  *hours *= sign;
  *minutes *= sign;
  *seconds *= sign;
  *milliseconds *= sign;
  *microseconds *= sign;
  *nanoseconds *= sign;
  return Just(true);
}

Maybe<DurationRecord> AddDuration(Isolate* isolate, const DurationRecord& dur1,
                                  const DurationRecord& dur2,
                                  Handle<Object> relative_to_obj) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  DurationRecord ret;
  // 1. Assert: y1, mon1, w1, d1, h1, min1, s1, ms1, mus1, ns1, y2, mon2, w2,
  // d2, h2, min2, s2, ms2, mus2, ns2 are integer Number values.
  // 2. Let largestUnit1 be ! DefaultTemporalLargestUnit(y1, mon1, w1, d1, h1,
  // min1, s1, ms1, mus1).
  Unit largest_unit1 = DefaultTemporalLargestUnit(isolate, dur1);

  // 3. Let largestUnit2 be ! DefaultTemporalLargestUnit(y2, mon2, w2, d2, h2,
  // min2, s2, ms2, mus2).
  Unit largest_unit2 = DefaultTemporalLargestUnit(isolate, dur2);
  // 4. Let largestUnit be ! LargerOfTwoTemporalUnits(largestUnit1,
  // largestUnit2).
  Unit largest_unit =
      LargerOfTwoTemporalUnits(isolate, largest_unit1, largest_unit2);
  // 5. If relativeTo is undefined, then
  if (relative_to_obj->IsUndefined()) {
    // a. If largestUnit is one of "year", "month", or "week", then
    if (largest_unit == Unit::kYear || largest_unit == Unit::kMonth ||
        largest_unit == Unit::kWeek) {
      // i. Throw a RangeError exception.
      THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                   NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                                   Nothing<DurationRecord>());
    }
    // b. Let result be ! BalanceDuration(d1 + d2, h1 + h2, min1 + min2, s1 +
    // s2, ms1 + ms2, mus1 + mus2, ns1 + ns2, largestUnit).
    ret.days = dur1.days + dur2.days;
    ret.hours = dur1.hours + dur2.hours;
    ret.minutes = dur1.minutes + dur2.minutes;
    ret.seconds = dur1.seconds + dur2.seconds;
    ret.milliseconds = dur1.milliseconds + dur2.milliseconds;
    ret.microseconds = dur1.microseconds + dur2.microseconds;
    ret.nanoseconds = dur1.nanoseconds + dur2.nanoseconds;
    Maybe<bool> maybe_result = BalanceDuration(
        isolate, &ret.days, &ret.hours, &ret.minutes, &ret.seconds,
        &ret.milliseconds, &ret.microseconds, &ret.nanoseconds, largest_unit);
    MAYBE_RETURN(maybe_result, Nothing<DurationRecord>());
    CHECK(maybe_result.FromJust());
    // c. Let years be 0.
    ret.years = 0;
    // d. Let months be 0.
    ret.months = 0;
    // e. Let weeks be 0.
    ret.weeks = 0;
    // f. Let days be result.[[Days]].

    // g. Let hours be result.[[Hours]].

    // h. Let minutes be result.[[Minutes]].

    // i. Let seconds be result.[[Seconds]].

    // j. Let milliseconds be result.[[Milliseconds]].

    // k. Let microseconds be result.[[Microseconds]].

    // l. Let nanoseconds be result.[[Nanoseconds]].

    // 6. Else if relativeTo has an [[InitializedTemporalPlainDateTime]]
    // internal slot, then
  } else if (relative_to_obj->IsJSTemporalPlainDateTime()) {
    // a. Let calendar be relativeTo.[[Calendar]].
    Handle<JSTemporalPlainDateTime> relative_to =
        Handle<JSTemporalPlainDateTime>::cast(relative_to_obj);
    Handle<JSReceiver> calendar =
        Handle<JSReceiver>(relative_to->calendar(), isolate);
    // b. Let datePart be ? CreateTemporalDate(relativeTo.[[ISOYear]],
    // relativeTo.[[ISOMonth]], relativeTo.[[ISODay]], calendar).
    Handle<JSTemporalPlainDate> date_part;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_part,
        CreateTemporalDate(isolate, relative_to->iso_year(),
                           relative_to->iso_month(), relative_to->iso_day(),
                           calendar),
        Nothing<DurationRecord>());
    // c. Let dateDuration1 be ? CreateTemporalDuration(y1, mon1, w1, d1, 0, 0,
    // 0, 0, 0, 0).
    Handle<JSTemporalDuration> date_duration1;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_duration1,
        CreateTemporalDuration(isolate, dur1.years, dur1.months, dur1.weeks,
                               dur1.days, 0, 0, 0, 0, 0, 0),
        Nothing<DurationRecord>());
    // d. Let dateDuration2 be ? CreateTemporalDuration(y2, mon2, w2, d2, 0, 0,
    // 0, 0, 0, 0).
    Handle<JSTemporalDuration> date_duration2;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_duration2,
        CreateTemporalDuration(isolate, dur2.years, dur2.months, dur2.weeks,
                               dur2.days, 0, 0, 0, 0, 0, 0),
        Nothing<DurationRecord>());
    // e. Let dateAdd be ? GetMethod(calendar, "dateAdd").
    Handle<Object> date_add;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_add,
        Object::GetMethod(calendar, factory->dateAdd_string()),
        Nothing<DurationRecord>());
    // f. Let firstAddOptions be ! OrdinaryObjectCreate(null).
    Handle<JSObject> first_add_options = factory->NewJSObjectWithNullProto();
    // g. Let intermediate be ? CalendarDateAdd(calendar, datePart,
    // dateDuration1, firstAddOptions, dateAdd).
    Handle<JSTemporalPlainDate> intermediate;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, intermediate,
        CalendarDateAdd(isolate, calendar, date_part, date_duration1,
                        first_add_options, date_add),
        Nothing<DurationRecord>());
    // h. Let secondAddOptions be ! OrdinaryObjectCreate(null).
    Handle<JSObject> second_add_options = factory->NewJSObjectWithNullProto();
    // i. Let end be ? CalendarDateAdd(calendar, intermediate, dateDuration2,
    // secondAddOptions, dateAdd).
    Handle<JSTemporalPlainDate> end;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, end,
        CalendarDateAdd(isolate, calendar, intermediate, date_duration2,
                        second_add_options, date_add),
        Nothing<DurationRecord>());
    // j. Let dateLargestUnit be ! LargerOfTwoTemporalUnits("day", largestUnit).
    Unit date_largest_unit =
        LargerOfTwoTemporalUnits(isolate, Unit::kDay, largest_unit);
    // k. Let differenceOptions be ! OrdinaryObjectCreate(null).
    Handle<JSObject> difference_options = factory->NewJSObjectWithNullProto();
    // l. Perform ! CreateDataPropertyOrThrow(differenceOptions, "largestUnit",
    // dateLargestUnit).
    CHECK(JSReceiver::CreateDataProperty(
              isolate, difference_options, factory->largestUnit_string(),
              UnitToString(isolate, date_largest_unit), Just(kThrowOnError))
              .FromJust());

    // m. Let dateDifference be ? CalendarDateUntil(calendar, datePart, end,
    // differenceOptions).
    Handle<JSTemporalDuration> date_difference;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_difference,
        CalendarDateUntil(isolate, calendar, date_part, end,
                          difference_options),
        Nothing<DurationRecord>());
    // n. Let result be ! BalanceDuration(dateDifference.[[Days]], h1 + h2, min1
    // + min2, s1 + s2, ms1 + ms2, mus1 + mus2, ns1 + ns2, largestUnit).
    ret.days = date_difference->days().Number();
    ret.hours = dur1.hours + dur2.hours;
    ret.minutes = dur1.minutes + dur2.minutes;
    ret.seconds = dur1.seconds + dur2.seconds;
    ret.milliseconds = dur1.milliseconds + dur2.milliseconds;
    ret.microseconds = dur1.microseconds + dur2.microseconds;
    ret.nanoseconds = dur1.nanoseconds + dur2.nanoseconds;
    Maybe<bool> maybe_result = BalanceDuration(
        isolate, &ret.days, &ret.hours, &ret.minutes, &ret.seconds,
        &ret.milliseconds, &ret.microseconds, &ret.nanoseconds, largest_unit);
    MAYBE_RETURN(maybe_result, Nothing<DurationRecord>());
    CHECK(maybe_result.FromJust());
    // o. Let years be dateDifference.[[Years]].
    ret.years = date_difference->years().Number();
    // p. Let months be dateDifference.[[Months]].
    ret.months = date_difference->months().Number();
    // q. Let weeks be dateDifference.[[Weeks]].
    ret.weeks = date_difference->weeks().Number();
    // r. Let days be result.[[Days]].

    // s. Let hours be result.[[Hours]].

    // t. Let minutes be result.[[Minutes]].

    // u. Let seconds be result.[[Seconds]].

    // v. Let milliseconds be result.[[Milliseconds]].

    // w. Let microseconds be result.[[Microseconds]].

    // x. Let nanoseconds be result.[[Nanoseconds]].

    // 7. Else,
  } else {
    // a. Assert: relativeTo has an [[InitializedTemporalZonedDateTime]]
    // internal slot.
    CHECK(relative_to_obj->IsJSTemporalZonedDateTime());
    Handle<JSTemporalZonedDateTime> relative_to =
        Handle<JSTemporalZonedDateTime>::cast(relative_to_obj);
    // b. Let timeZone be relativeTo.[[TimeZone]].
    Handle<JSReceiver> time_zone =
        Handle<JSReceiver>(relative_to->time_zone(), isolate);
    // c. Let calendar be relativeTo.[[Calendar]].
    Handle<JSReceiver> calendar =
        Handle<JSReceiver>(relative_to->calendar(), isolate);
    // d. Let intermediateNs be ? AddZonedDateTime(relativeTo.[[Nanoseconds]],
    // timeZone, calendar, y1, mon1, w1, d1, h1, min1, s1, ms1, mus1, ns1).
    Handle<BigInt> intermediate_ns;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, intermediate_ns,
        AddZonedDateTime(isolate,
                         Handle<BigInt>(relative_to->nanoseconds(), isolate),
                         time_zone, calendar, dur1),
        Nothing<DurationRecord>());
    // e. Let endNs be ? AddZonedDateTime(intermediateNs, timeZone, calendar,
    // y2, mon2, w2, d2, h2, min2, s2, ms2, mus2, ns2).
    Handle<BigInt> end_ns;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, end_ns,
        AddZonedDateTime(isolate, intermediate_ns, time_zone, calendar, dur2),
        Nothing<DurationRecord>());
    // f. If largestUnit is not one of "year", "month", "week", or "day", then
    if (largest_unit != Unit::kYear && largest_unit != Unit::kMonth &&
        largest_unit != Unit::kWeek && largest_unit != Unit::kDay) {
      // i. Let diffNs be ! DifferenceInstant(relativeTo.[[Nanoseconds]], endNs,
      // 1, "nanosecond", "halfExpand").
      Handle<BigInt> diff_ns;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, diff_ns,
          DifferenceInstant(
              isolate, Handle<BigInt>(relative_to->nanoseconds(), isolate),
              end_ns, 1, Unit::kNanosecond, RoundingMode::kHalfExpand),
          Nothing<DurationRecord>());
      // ii. Let result be ! BalanceDuration(0, 0, 0, 0, 0, 0, diffNs,
      // largestUnit).
      ret.days = 0;
      ret.hours = 0;
      ret.minutes = 0;
      ret.seconds = 0;
      ret.milliseconds = 0;
      ret.microseconds = 0;
      ret.nanoseconds = diff_ns->AsInt64();
      Maybe<bool> maybe_result = BalanceDuration(
          isolate, &ret.days, &ret.hours, &ret.minutes, &ret.seconds,
          &ret.milliseconds, &ret.microseconds, &ret.nanoseconds, largest_unit);
      MAYBE_RETURN(maybe_result, Nothing<DurationRecord>());
      CHECK(maybe_result.FromJust());
      // iii. Let years be 0.
      ret.years = 0;
      // iv. Let months be 0.
      ret.months = 0;
      // v. Let weeks be 0.
      ret.weeks = 0;
      // vi. Let days be 0.
      ret.days = 0;
      // vii. Let hours be result.[[Hours]].

      // viii. Let minutes be result.[[Minutes]].

      // ix. Let seconds be result.[[Seconds]].

      // x. Let milliseconds be result.[[Milliseconds]].

      // xi. Let microseconds be result.[[Microseconds]].

      // xii. Let nanoseconds be result.[[Nanoseconds]].

      // g. Else,
    } else {
      // i. Let result be ? DifferenceZonedDateTime(relativeTo.[[Nanoseconds]],
      // endNs, timeZone, calendar, largestUnit).
      Maybe<DurationRecord> maybe_result = DifferenceZonedDateTime(
          isolate, Handle<BigInt>(relative_to->nanoseconds(), isolate), end_ns,
          time_zone, calendar, largest_unit, factory->undefined_value());
      MAYBE_RETURN(maybe_result, Nothing<DurationRecord>());
      ret = maybe_result.FromJust();
      // ii. Let years be result.[[Years]].

      // iii. Let months be result.[[Months]].

      // iv. Let weeks be result.[[Weeks]].

      // v. Let days be result.[[Days]].

      // vi. Let hours be result.[[Hours]].

      // vii. Let minutes be result.[[Minutes]].

      // viii. Let seconds be result.[[Seconds]].

      // ix. Let milliseconds be result.[[Milliseconds]].

      // x. Let microseconds be result.[[Microseconds]].

      // xi. Let nanoseconds be result.[[Nanoseconds]].
    }
  }
  // 8. If ! IsValidDuration(years, months, weeks, days, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds) is false, throw a
  // RangeError exception.
  if (!IsValidDuration(isolate, ret)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                                 Nothing<DurationRecord>());
  }
  // 9. Return the new Record { [[Years]]: years, [[Months]]: months, [[Weeks]]:
  // weeks, [[Days]]: days, [[Hours]]: hours, [[Minutes]]: minutes, [[Seconds]]:
  // seconds, [[Milliseconds]]: milliseconds, [[Microseconds]]: microseconds,
  // [[Nanoseconds]]: nanoseconds }.
  return Just(ret);
}

// #sec-temporal-adjustroundeddurationdays
Maybe<DurationRecord> AdjustRoundedDurationDays(
    Isolate* isolate, const DurationRecord& duration, double increment,
    Unit unit, RoundingMode rounding_mode, Handle<Object> relative_to_obj) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  // 1. If relativeTo is not present; or Type(relativeTo) is not Object; or
  // relativeTo does not have an [[InitializedTemporalZonedDateTime]] internal
  // slot; or unit is one of "year", "month", "week", or "day"; or unit is
  // "nanosecond" and increment is 1, then
  if (relative_to_obj->IsUndefined() ||
      (!relative_to_obj->IsJSTemporalZonedDateTime()) || unit == Unit::kYear ||
      unit == Unit::kMonth || unit == Unit::kWeek || unit == Unit::kDay ||
      (unit == Unit::kNanosecond && increment == 1)) {
    // a. Return the Record { [[Years]]: years, [[Months]]: months, [[Weeks]]:
    // weeks, [[Days]]: days, [[Hours]]: hours, [[Minutes]]: minutes,
    // [[Seconds]]: seconds, [[Milliseconds]]: milliseconds, [[Microseconds]]:
    // microseconds, [[Nanoseconds]]: nanoseconds }.
    return Just(duration);
  }
  Handle<JSTemporalZonedDateTime> relative_to =
      Handle<JSTemporalZonedDateTime>::cast(relative_to_obj);
  // 2. Let timeRemainderNs be ? TotalDurationNanoseconds(0, hours, minutes,
  // seconds, milliseconds, microseconds, nanoseconds, 0).
  double time_remainder_ns_double = TotalDurationNanoseconds(
      isolate, 0, duration.hours, duration.minutes, duration.seconds,
      duration.milliseconds, duration.microseconds, duration.nanoseconds, 0);
  // 3. Let direction be ! ℝ(Sign(𝔽(timeRemainderNs))).
  double direction = time_remainder_ns_double > 0
                         ? 1
                         : (time_remainder_ns_double == 0 ? 0 : -1);
  // 4. Let dayStart be ? AddZonedDateTime(relativeTo.[[Nanoseconds]],
  // relativeTo.[[TimeZone]], relativeTo.[[Calendar]], years, months, weeks,
  // days, 0, 0, 0, 0, 0, 0).
  Handle<BigInt> relative_to_nanoseconds =
      Handle<BigInt>(relative_to->nanoseconds(), isolate);
  Handle<JSReceiver> relative_to_time_zone =
      Handle<JSReceiver>(relative_to->time_zone(), isolate);
  Handle<JSReceiver> relative_to_calendar =
      Handle<JSReceiver>(relative_to->calendar(), isolate);
  Handle<BigInt> day_start;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, day_start,
      AddZonedDateTime(isolate, relative_to_nanoseconds, relative_to_time_zone,
                       relative_to_calendar,
                       {duration.years, duration.months, duration.weeks,
                        duration.days, 0, 0, 0, 0, 0, 0}),
      Nothing<DurationRecord>());
  // 5. Let dayEnd be ? AddZonedDateTime(dayStart, relativeTo.[[TimeZone]],
  // relativeTo.[[Calendar]], 0, 0, 0, direction, 0, 0, 0, 0, 0, 0).
  Handle<BigInt> day_end;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, day_end,
      AddZonedDateTime(isolate, day_start, relative_to_time_zone,
                       relative_to_calendar,
                       {0, 0, 0, direction, 0, 0, 0, 0, 0, 0}),
      Nothing<DurationRecord>());
  // 6. Let dayLengthNs be ℝ(dayEnd − dayStart).
  Handle<BigInt> day_length_ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, day_length_ns, BigInt::Subtract(isolate, day_end, day_start),
      Nothing<DurationRecord>());
  // 7. If (timeRemainderNs − dayLengthNs) × direction < 0, then
  Handle<BigInt> time_remainder_ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_remainder_ns,
      BigInt::FromNumber(isolate, factory->NewNumber(time_remainder_ns_double)),
      Nothing<DurationRecord>());

  Handle<BigInt> diff;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, diff,
      BigInt::Subtract(isolate, time_remainder_ns, day_length_ns),
      Nothing<DurationRecord>());
  if (diff->AsInt64() * direction < 0) {
    // a. Return the Record { [[Years]]: years, [[Months]]: months, [[Weeks]]:
    // weeks, [[Days]]: days, [[Hours]]: hours, [[Minutes]]: minutes,
    // [[Seconds]]: seconds, [[Milliseconds]]: milliseconds, [[Microseconds]]:
    // microseconds, [[Nanoseconds]]: nanoseconds }.
    return Just(duration);
  }
  // 8. Set timeRemainderNs to ? RoundTemporalInstant(ℤ(timeRemainderNs −
  // dayLengthNs), increment, unit, roundingMode).
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_remainder_ns,
      RoundTemporalInstant(isolate, diff, increment, unit, rounding_mode),
      Nothing<DurationRecord>());

  // 9. Let adjustedDateDuration be ? AddDuration(years, months, weeks, days, 0,
  // 0, 0, 0, 0, 0, 0, 0, 0, direction, 0, 0, 0, 0, 0, 0, relativeTo).
  Maybe<DurationRecord> maybe_adjusted_date_duration =
      AddDuration(isolate,
                  {duration.years, duration.months, duration.weeks,
                   duration.days, 0, 0, 0, 0, 0, 0},
                  {0, 0, 0, direction, 0, 0, 0, 0, 0, 0}, relative_to_obj);
  MAYBE_RETURN(maybe_adjusted_date_duration, Nothing<DurationRecord>());
  DurationRecord adjusted = maybe_adjusted_date_duration.FromJust();

  // 10. Let adjustedTimeDuration be ? BalanceDuration(0, 0, 0, 0, 0, 0,
  // timeRemainderNs, "hour").
  adjusted.days = adjusted.hours = adjusted.minutes = adjusted.seconds =
      adjusted.milliseconds = adjusted.microseconds = 0;
  adjusted.nanoseconds = time_remainder_ns->AsInt64();
  Maybe<bool> maybe_adjusted_time_duration = BalanceDuration(
      isolate, &adjusted.days, &adjusted.hours, &adjusted.minutes,
      &adjusted.seconds, &adjusted.milliseconds, &adjusted.microseconds,
      &adjusted.nanoseconds, Unit::kHour);
  MAYBE_RETURN(maybe_adjusted_time_duration, Nothing<DurationRecord>());
  CHECK(maybe_adjusted_time_duration.FromJust());
  // 11. Return the Record { [[Years]]: adjustedDateDuration.[[Years]],
  // [[Months]]: adjustedDateDuration.[[Months]], [[Weeks]]:
  // adjustedDateDuration.[[Weeks]], [[Days]]: adjustedDateDuration.[[Days]],
  // [[Hours]]: adjustedTimeDuration.[[Hours]], [[Minutes]]:
  // adjustedTimeDuration.[[Minutes]], [[Seconds]]:
  // adjustedTimeDuration.[[Seconds]], [[Milliseconds]]:
  // adjustedTimeDuration.[[Milliseconds]], [[Microseconds]]:
  // adjustedTimeDuration.[[Microseconds]], [[Nanoseconds]]:
  // adjustedTimeDuration.[[Nanoseconds]] }.
  return Just(adjusted);
}

// #sec-temporal-addinstant
MaybeHandle<BigInt> AddZonedDateTime(Isolate* isolate,
                                     Handle<BigInt> epoch_nanoseconds,
                                     Handle<JSReceiver> time_zone,
                                     Handle<JSReceiver> calendar,
                                     const DurationRecord& duration) {
  TEMPORAL_ENTER_FUNC

  // 1. If options is not present, set options to ! OrdinaryObjectCreate(null).
  Handle<JSReceiver> options = isolate->factory()->NewJSObjectWithNullProto();
  return AddZonedDateTime(isolate, epoch_nanoseconds, time_zone, calendar,
                          duration, options);
}

// #sec-temporal-addzoneddatetime
MaybeHandle<BigInt> AddZonedDateTime(Isolate* isolate,
                                     Handle<BigInt> epoch_nanoseconds,
                                     Handle<JSReceiver> time_zone,
                                     Handle<JSReceiver> calendar,
                                     const DurationRecord& duration,
                                     Handle<JSReceiver> options) {
  TEMPORAL_ENTER_FUNC

  // 2. If all of years, months, weeks, and days are 0, then
  if (duration.years == 0 && duration.months == 0 && duration.weeks == 0 &&
      duration.days == 0) {
    // a. Return ! AddInstant(epochNanoseconds, hours, minutes, seconds,
    // milliseconds, microseconds, nanoseconds).
    return AddInstant(isolate, epoch_nanoseconds, duration.hours,
                      duration.minutes, duration.seconds, duration.milliseconds,
                      duration.microseconds, duration.nanoseconds);
  }
  // 3. Let instant be ! CreateTemporalInstant(epochNanoseconds).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, instant,
                             CreateTemporalInstant(isolate, epoch_nanoseconds),
                             BigInt);

  // 4. Let temporalDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, calendar).
  Handle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_time,
      BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, instant, calendar),
      BigInt);
  // 5. Let datePart be ? CreateTemporalDate(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]], calendar).
  Handle<JSTemporalPlainDate> date_part;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_part,
      CreateTemporalDate(isolate, temporal_date_time->iso_year(),
                         temporal_date_time->iso_month(),
                         temporal_date_time->iso_day(), calendar),
      BigInt);
  // 6. Let dateDuration be ? CreateTemporalDuration(years, months, weeks, days,
  // 0, 0, 0, 0, 0, 0).
  Handle<JSTemporalDuration> date_duration;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_duration,
      CreateTemporalDuration(isolate, duration.years, duration.months,
                             duration.weeks, duration.days, 0, 0, 0, 0, 0, 0),
      BigInt);
  // 7. Let addedDate be ? CalendarDateAdd(calendar, datePart, dateDuration,
  // options).
  Handle<JSTemporalPlainDate> added_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, added_date,
      CalendarDateAdd(isolate, calendar, date_part, date_duration, options),
      BigInt);
  // 8. Let intermediateDateTime be ?
  // CreateTemporalDateTime(addedDate.[[ISOYear]], addedDate.[[ISOMonth]],
  // addedDate.[[ISODay]], temporalDateTime.[[ISOHour]],
  // temporalDateTime.[[ISOMinute]], temporalDateTime.[[ISOSecond]],
  // temporalDateTime.[[ISOMillisecond]], temporalDateTime.[[ISOMicrosecond]],
  // temporalDateTime.[[ISONanosecond]], calendar).
  Handle<JSTemporalPlainDateTime> intermediate_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, intermediate_date_time,
      CreateTemporalDateTime(
          isolate, added_date->iso_year(), added_date->iso_month(),
          added_date->iso_day(), temporal_date_time->iso_hour(),
          temporal_date_time->iso_minute(), temporal_date_time->iso_second(),
          temporal_date_time->iso_millisecond(),
          temporal_date_time->iso_microsecond(),
          temporal_date_time->iso_nanosecond(), calendar),
      BigInt);
  // 9. Let intermediateInstant be ? BuiltinTimeZoneGetInstantFor(timeZone,
  // intermediateDateTime, "compatible").
  Handle<JSTemporalInstant> intermediate_instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, intermediate_instant,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, intermediate_date_time,
                                   Disambiguation::kCompatible),
      BigInt);
  // 10. Return ! AddInstant(intermediateInstant.[[Nanoseconds]], hours,
  // minutes, seconds, milliseconds, microseconds, nanoseconds).
  return AddInstant(
      isolate, Handle<BigInt>(intermediate_instant->nanoseconds(), isolate),
      duration.hours, duration.minutes, duration.seconds, duration.milliseconds,
      duration.microseconds, duration.nanoseconds);
}

// #sec-temporal-differenceinstant
MaybeHandle<BigInt> DifferenceInstant(Isolate* isolate, Handle<BigInt> ns1,
                                      Handle<BigInt> ns2,
                                      double rounding_increment,
                                      Unit smallest_unit,
                                      RoundingMode rounding_mode) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: Type(ns1) is BigInt.
  // 2. Assert: Type(ns2) is BigInt.
  // 3. Return ! RoundTemporalInstant(ns2 − ns1, roundingIncrement,
  // smallestUnit, roundingMode).
  Handle<BigInt> diff;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, diff, BigInt::Subtract(isolate, ns2, ns1),
                             BigInt);

  return RoundTemporalInstant(isolate, diff, rounding_increment, smallest_unit,
                              rounding_mode);
}

// #sec-temporal-differencezoneddatetime
Maybe<DurationRecord> DifferenceZonedDateTime(
    Isolate* isolate, Handle<BigInt> ns1, Handle<BigInt> ns2,
    Handle<JSReceiver> time_zone, Handle<JSReceiver> calendar,
    Unit largest_unit, Handle<Object> options) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  DurationRecord result;
  // 1. Assert: Type(ns1) is BigInt.
  // 2. Assert: Type(ns2) is BigInt.
  // 3. If ns1 is ns2, then
  if (BigInt::CompareToBigInt(ns1, ns2) == ComparisonResult::kEqual) {
    // a. Return the new Record { [[Years]]: 0, [[Months]]: 0, [[Weeks]]: 0,
    // [[Days]]: 0, [[Hours]]: 0, [[Minutes]]: 0, [[Seconds]]: 0,
    // [[Milliseconds]]: 0, [[Microseconds]]: 0, [[Nanoseconds]]: 0 }.
    result.years = result.months = result.weeks = result.days = result.hours =
        result.minutes = result.seconds = result.milliseconds =
            result.microseconds = result.nanoseconds = 0;
    return Just(result);
  }
  // 4. Let startInstant be ! CreateTemporalInstant(ns1).
  Handle<JSTemporalInstant> start_instant;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, start_instant,
                                   CreateTemporalInstant(isolate, ns1),
                                   Nothing<DurationRecord>());
  // 5. Let startDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // startInstant, calendar).
  Handle<JSTemporalPlainDateTime> start_date_time;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, start_date_time,
      BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, start_instant,
                                         calendar),
      Nothing<DurationRecord>());
  // 6. Let endInstant be ! CreateTemporalInstant(ns2).
  Handle<JSTemporalInstant> end_instant;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, end_instant,
                                   CreateTemporalInstant(isolate, ns2),
                                   Nothing<DurationRecord>());
  // 7. Let endDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // endInstant, calendar).
  Handle<JSTemporalPlainDateTime> end_date_time;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, end_date_time,
      BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, end_instant,
                                         calendar),
      Nothing<DurationRecord>());
  // 8. Let dateDifference be ? DifferenceISODateTime(startDateTime.[[ISOYear]],
  // startDateTime.[[ISOMonth]], startDateTime.[[ISODay]],
  // startDateTime.[[ISOHour]], startDateTime.[[ISOMinute]],
  // startDateTime.[[ISOSecond]], startDateTime.[[ISOMillisecond]],
  // startDateTime.[[ISOMicrosecond]], startDateTime.[[ISONanosecond]],
  // endDateTime.[[ISOYear]], endDateTime.[[ISOMonth]], endDateTime.[[ISODay]],
  // endDateTime.[[ISOHour]], endDateTime.[[ISOMinute]],
  // endDateTime.[[ISOSecond]], endDateTime.[[ISOMillisecond]],
  // endDateTime.[[ISOMicrosecond]], endDateTime.[[ISONanosecond]], calendar,
  // largestUnit, options).
  Maybe<DurationRecord> maybe_date_difference = DifferenceISODateTime(
      isolate, start_date_time->iso_year(), start_date_time->iso_month(),
      start_date_time->iso_day(), start_date_time->iso_hour(),
      start_date_time->iso_minute(), start_date_time->iso_second(),
      start_date_time->iso_millisecond(), start_date_time->iso_microsecond(),
      start_date_time->iso_nanosecond(), end_date_time->iso_year(),
      end_date_time->iso_month(), end_date_time->iso_day(),
      end_date_time->iso_hour(), end_date_time->iso_minute(),
      end_date_time->iso_second(), end_date_time->iso_millisecond(),
      end_date_time->iso_microsecond(), end_date_time->iso_nanosecond(),
      calendar, Unit::kDay, factory->undefined_value());
  MAYBE_RETURN(maybe_date_difference, Nothing<DurationRecord>());

  result = maybe_date_difference.FromJust();

  // 9. Let intermediateNs be ? AddZonedDateTime(ns1, timeZone, calendar,
  // dateDifference.[[Years]], dateDifference.[[Months]],
  // dateDifference.[[Weeks]], 0, 0, 0, 0, 0, 0, 0).
  Handle<BigInt> intermediate_ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, intermediate_ns,
      AddZonedDateTime(
          isolate, ns1, time_zone, calendar,
          {result.years, result.months, result.weeks, 0, 0, 0, 0, 0, 0, 0}),
      Nothing<DurationRecord>());
  // 10. Let timeRemainderNs be ns2 − intermediateNs.
  Handle<BigInt> time_remainder_ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_remainder_ns,
      BigInt::Subtract(isolate, ns2, intermediate_ns),
      Nothing<DurationRecord>());

  // 11. Let intermediate be ? CreateTemporalZonedDateTime(intermediateNs,
  // timeZone, calendar).
  Handle<JSTemporalZonedDateTime> intermediate;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, intermediate,
      CreateTemporalZonedDateTime(isolate, intermediate_ns, time_zone,
                                  calendar),
      Nothing<DurationRecord>());

  // 12. Let result be ? NanosecondsToDays(timeRemainderNs, intermediate).
  double result_day_length;
  double result_days;
  Maybe<bool> maybe_result =
      NanosecondsToDays(isolate, time_remainder_ns, intermediate, &result_days,
                        &(result.nanoseconds), &result_day_length);
  MAYBE_RETURN(maybe_result, Nothing<DurationRecord>());

  // 13. Let timeDifference be ! BalanceDuration(0, 0, 0, 0, 0, 0,
  // result.[[Nanoseconds]], "hour").
  result.days = result.hours = result.minutes = result.seconds =
      result.milliseconds = result.microseconds = 0;
  Maybe<bool> maybe_time_difference = BalanceDuration(
      isolate, &(result.days), &(result.hours), &(result.minutes),
      &(result.seconds), &(result.milliseconds), &(result.microseconds),
      &(result.nanoseconds), Unit::kHour);
  MAYBE_RETURN(maybe_time_difference, Nothing<DurationRecord>());
  CHECK(maybe_time_difference.FromJust());

  // 14. Return the new Record { [[Years]]: dateDifference.[[Years]],
  // [[Months]]: dateDifference.[[Months]], [[Weeks]]: dateDifference.[[Weeks]],
  // [[Days]]: result.[[Days]], [[Hours]]: timeDifference.[[Hours]],
  // [[Minutes]]: timeDifference.[[Minutes]], [[Seconds]]:
  // timeDifference.[[Seconds]], [[Milliseconds]]:
  // timeDifference.[[Milliseconds]], [[Microseconds]]:
  // timeDifference.[[Microseconds]], [[Nanoseconds]]:
  // timeDifference.[[Nanoseconds]] }.
  result.days = result_days;
  return Just(result);
}

// #sec-temporal-nanosecondstodays
Maybe<bool> NanosecondsToDays(Isolate* isolate, double nanoseconds,
                              Handle<Object> relative_to_obj,
                              double* result_days, double* result_nanoseconds,
                              double* result_day_length) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  Handle<BigInt> ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, ns, BigInt::FromNumber(isolate, factory->NewNumber(nanoseconds)),
      Nothing<bool>());
  return NanosecondsToDays(isolate, ns, relative_to_obj, result_days,
                           result_nanoseconds, result_day_length);
}

Maybe<bool> NanosecondsToDays(Isolate* isolate, Handle<BigInt> nanoseconds,
                              Handle<Object> relative_to_obj,
                              double* result_days, double* result_nanoseconds,
                              double* result_day_length) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  // 1. Assert: Type(nanoseconds) is BigInt.
  // 2. Set nanoseconds to ℝ(nanoseconds).
  // 3. Let sign be ! ℝ(Sign(𝔽(nanoseconds))).
  ComparisonResult sign =
      BigInt::CompareToBigInt(nanoseconds, BigInt::FromInt64(isolate, 0));
  // 4. Let dayLengthNs be 8.64 × 1013.
  Handle<BigInt> day_length_ns = BigInt::FromInt64(isolate, 86400000000000);
  // 5. If sign is 0, then
  if (sign == ComparisonResult::kEqual) {
    // a. Return the new Record { [[Days]]: 0, [[Nanoseconds]]: 0,
    // [[DayLength]]: dayLengthNs }.
    *result_days = 0;
    *result_nanoseconds = 0;
    *result_day_length = day_length_ns->AsInt64();
    return Just(true);
  }
  // 6. If Type(relativeTo) is not Object or relativeTo does not have an
  // [[InitializedTemporalZonedDateTime]] internal slot, then
  if (!relative_to_obj->IsJSTemporalZonedDateTime()) {
    // a. Return the new Record { [[Days]]: the integral part of nanoseconds /
    // dayLengthNs, [[Nanoseconds]]: nanoseconds modulo dayLengthNs,
    // [[DayLength]]: dayLengthNs }.
    Handle<BigInt> days_bigint;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, days_bigint,
        BigInt::Divide(isolate, nanoseconds, day_length_ns), Nothing<bool>());
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, nanoseconds,
        BigInt::Remainder(isolate, nanoseconds, day_length_ns),
        Nothing<bool>());
    *result_days = days_bigint->AsInt64();
    *result_nanoseconds = nanoseconds->AsInt64();
    *result_day_length = day_length_ns->AsInt64();
    return Just(true);
  }
  Handle<JSTemporalZonedDateTime> relative_to =
      Handle<JSTemporalZonedDateTime>::cast(relative_to_obj);
  // 7. Let startNs be ℝ(relativeTo.[[Nanoseconds]]).
  Handle<BigInt> start_ns = Handle<BigInt>(relative_to->nanoseconds(), isolate);
  // 8. Let startInstant be ! CreateTemporalInstant(ℤ(sartNs)).
  Handle<JSTemporalInstant> start_instant;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, start_instant,
      CreateTemporalInstant(
          isolate, Handle<BigInt>(relative_to->nanoseconds(), isolate)),
      Nothing<bool>());

  // 9. Let startDateTime be ?
  // BuiltinTimeZoneGetPlainDateTimeFor(relativeTo.[[TimeZone]], startInstant,
  // relativeTo.[[Calendar]]).
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(relative_to->time_zone(), isolate);
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(relative_to->calendar(), isolate);
  Handle<JSTemporalPlainDateTime> start_date_time;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, start_date_time,
      BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, start_instant,
                                         calendar),
      Nothing<bool>());

  // 10. Let endNs be startNs + nanoseconds.
  Handle<BigInt> end_ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, end_ns,
                                   BigInt::Add(isolate, start_ns, nanoseconds),
                                   Nothing<bool>());

  // 11. Let endInstant be ! CreateTemporalInstant(ℤ(endNs)).
  Handle<JSTemporalInstant> end_instant;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, end_instant,
                                   CreateTemporalInstant(isolate, end_ns),
                                   Nothing<bool>());
  // 12. Let endDateTime be ?
  // BuiltinTimeZoneGetPlainDateTimeFor(relativeTo.[[TimeZone]], endInstant,
  // relativeTo.[[Calendar]]).
  Handle<JSTemporalPlainDateTime> end_date_time;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, end_date_time,
      BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone, end_instant,
                                         calendar),
      Nothing<bool>());

  // 13. Let dateDifference be ?
  // DifferenceISODateTime(startDateTime.[[ISOYear]],
  // startDateTime.[[ISOMonth]], startDateTime.[[ISODay]],
  // startDateTime.[[ISOHour]], startDateTime.[[ISOMinute]],
  // startDateTime.[[ISOSecond]], startDateTime.[[ISOMillisecond]],
  // startDateTime.[[ISOMicrosecond]], startDateTime.[[ISONanosecond]],
  // endDateTime.[[ISOYear]], endDateTime.[[ISOMonth]], endDateTime.[[ISODay]],
  // endDateTime.[[ISOHour]], endDateTime.[[ISOMinute]],
  // endDateTime.[[ISOSecond]], endDateTime.[[ISOMillisecond]],
  // endDateTime.[[ISOMicrosecond]], endDateTime.[[ISONanosecond]],
  // relativeTo.[[Calendar]], "day").
  Maybe<DurationRecord> maybe_date_difference = DifferenceISODateTime(
      isolate, start_date_time->iso_year(), start_date_time->iso_month(),
      start_date_time->iso_day(), start_date_time->iso_hour(),
      start_date_time->iso_minute(), start_date_time->iso_second(),
      start_date_time->iso_millisecond(), start_date_time->iso_microsecond(),
      start_date_time->iso_nanosecond(), end_date_time->iso_year(),
      end_date_time->iso_month(), end_date_time->iso_day(),
      end_date_time->iso_hour(), end_date_time->iso_minute(),
      end_date_time->iso_second(), end_date_time->iso_millisecond(),
      end_date_time->iso_microsecond(), end_date_time->iso_nanosecond(),
      calendar, Unit::kDay, factory->undefined_value());
  MAYBE_RETURN(maybe_date_difference, Nothing<bool>());

  DurationRecord date_difference = maybe_date_difference.FromJust();
  // 14. Let days be dateDifference.[[Days]].
  double days = date_difference.days;

  // 15. Let intermediateNs be ℝ(? AddZonedDateTime(ℤ(startNs),
  // relativeTo.[[TimeZone]], relativeTo.[[Calendar]], 0, 0, 0, days, 0, 0, 0,
  // 0, 0, 0)).
  Handle<BigInt> intermediate_ns;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, intermediate_ns,
      AddZonedDateTime(isolate, start_ns, time_zone, calendar,
                       {0, 0, 0, days, 0, 0, 0, 0, 0, 0}),
      Nothing<bool>());

  // 16. If sign is 1, then
  if (sign == ComparisonResult::kGreaterThan) {
    // a. Repeat, while days > 0 and intermediateNs > endNs,
    while (days > 0 && BigInt::CompareToBigInt(intermediate_ns, end_ns) ==
                           ComparisonResult::kGreaterThan) {
      // i. Set days to days − 1.
      days -= 1;
      // ii. Set intermediateNs to ℝ(? AddZonedDateTime(ℤ(startNs),
      // relativeTo.[[TimeZone]], relativeTo.[[Calendar]], 0, 0, 0, days, 0, 0,
      // 0, 0, 0, 0)).
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, intermediate_ns,
          AddZonedDateTime(isolate, start_ns, time_zone, calendar,
                           {0, 0, 0, days, 0, 0, 0, 0, 0, 0}),
          Nothing<bool>());
    }
  }

  // 17. Set nanoseconds to endNs − intermediateNs.
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, nanoseconds, BigInt::Subtract(isolate, end_ns, intermediate_ns),
      Nothing<bool>());

  // 18. Let done be false.
  bool done = false;

  // 19. Repeat, while done is false,
  double sign_double = (sign == ComparisonResult::kGreaterThan)
                           ? 1
                           : ((sign == ComparisonResult::kLessThan) ? -1 : 0);
  while (!done) {
    // a. Let oneDayFartherNs be ℝ(? AddZonedDateTime(ℤ(intermediateNs),
    // relativeTo.[[TimeZone]], relativeTo.[[Calendar]], 0, 0, 0, sign, 0, 0, 0,
    // 0, 0, 0)).
    Handle<BigInt> one_day_father_ns;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, one_day_father_ns,
        AddZonedDateTime(isolate, intermediate_ns, time_zone, calendar,
                         {0, 0, 0, sign_double, 0, 0, 0, 0, 0, 0}),
        Nothing<bool>());

    // b. Set dayLengthNs to oneDayFartherNs − intermediateNs.
    Handle<BigInt> day_length_ns;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, day_length_ns,
        BigInt::Subtract(isolate, one_day_father_ns, intermediate_ns),
        Nothing<bool>());

    // c. If (nanoseconds − dayLengthNs) × sign ≥ 0, then
    ComparisonResult diff = BigInt::CompareToBigInt(nanoseconds, day_length_ns);
    if (sign != diff && (sign == ComparisonResult::kLessThan ||
                         diff == ComparisonResult::kLessThan)) {
      // i. Set nanoseconds to nanoseconds − dayLengthNs.
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, nanoseconds,
          BigInt::Subtract(isolate, nanoseconds, day_length_ns),
          Nothing<bool>());

      // ii. Set intermediateNs to oneDayFartherNs.
      intermediate_ns = one_day_father_ns;

      // iii. Set days to days + sign.
      if (sign == ComparisonResult::kGreaterThan) {
        days += 1;
      } else if (sign == ComparisonResult::kLessThan) {
        days -= 1;
      }
      // d. Else,
    } else {
      // i. Set done to true.
      done = true;
    }
  }

  // 20. Return the new Record { [[Days]]: days, [[Nanoseconds]]: nanoseconds,
  // [[DayLength]]: abs(dayLengthNs) }.
  *result_days = days;
  *result_nanoseconds = nanoseconds->AsInt64();
  *result_day_length = day_length_ns->AsInt64();
  return Just(true);
}

Maybe<DurationRecord> DifferenceISODateTime(
    Isolate* isolate, double y1, double mon1, double d1, double h1, double min1,
    double s1, double ms1, double mus1, double ns1, double y2, double mon2,
    double d2, double h2, double min2, double s2, double ms2, double mus2,
    double ns2, Handle<JSReceiver> calendar, Unit largest_unit,
    Handle<Object> options_obj) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  DurationRecord result;
  // 1. Assert: y1, mon1, d1, h1, min1, s1, ms1, mus1, ns1, y2, mon2, d2, h2,
  // min2, s2, ms2, mus2, and ns2 are integers.
  // 2. If options is not present, set options to ! OrdinaryObjectCreate(null).
  Handle<JSReceiver> options;
  if (options_obj->IsUndefined()) {
    options = factory->NewJSObjectWithNullProto();
  } else {
    CHECK(options_obj->IsJSReceiver());
    options = Handle<JSReceiver>::cast(options_obj);
  }
  // 3. Let timeDifference be ! DifferenceTime(h1, min1, s1, ms1, mus1, ns1, h2,
  // min2, s2, ms2, mus2, ns2).
  DurationRecord time_difference = DifferenceTime(
      isolate, h1, min1, s1, ms1, mus1, ns1, h2, min2, s2, ms2, mus2, ns2);

  result.hours = time_difference.hours;
  result.minutes = time_difference.minutes;
  result.seconds = time_difference.seconds;
  result.milliseconds = time_difference.milliseconds;
  result.microseconds = time_difference.microseconds;
  result.nanoseconds = time_difference.nanoseconds;

  // 4. Let timeSign be ! DurationSign(0, 0, 0, timeDifference.[[Days]],
  // timeDifference.[[Hours]], timeDifference.[[Minutes]],
  // timeDifference.[[Seconds]], timeDifference.[[Milliseconds]],
  // timeDifference.[[Microseconds]], timeDifference.[[Nanoseconds]]).
  int32_t time_sign = DurationSign(isolate, time_difference);
  // 5. Let dateSign be ! CompareISODate(y2, mon2, d2, y1, mon1, d1).
  int32_t date_sign = CompareISODate(isolate, y2, mon2, d2, y1, mon1, d1);
  // 6. Let balanceResult be ! BalanceISODate(y1, mon1, d1 +
  // timeDifference.[[Days]]).
  int32_t balanced_year = y1;
  int32_t balanced_month = mon1;
  int32_t balanced_day = d1 + time_difference.days;
  BalanceISODate(isolate, &balanced_year, &balanced_month, &balanced_day);
  // 7. If timeSign is -dateSign, then
  if (time_sign == -date_sign) {
    // a. Set balanceResult be ! BalanceISODate(balanceResult.[[Year]],
    // balanceResult.[[Month]], balanceResult.[[Day]] - timeSign).
    balanced_day -= time_sign;
    BalanceISODate(isolate, &balanced_year, &balanced_month, &balanced_day);
    // b. Set timeDifference to ? BalanceDuration(-timeSign,
    // timeDifference.[[Hours]], timeDifference.[[Minutes]],
    // timeDifference.[[Seconds]], timeDifference.[[Milliseconds]],
    // timeDifference.[[Microseconds]], timeDifference.[[Nanoseconds]],
    // largestUnit).
    result.days = -time_sign;
    result.hours = time_difference.hours;
    result.minutes = time_difference.minutes;
    result.seconds = time_difference.seconds;
    result.milliseconds = time_difference.milliseconds;
    result.microseconds = time_difference.microseconds;
    result.nanoseconds = time_difference.nanoseconds;

    Maybe<bool> maybe_time_difference = BalanceDuration(
        isolate, &(result.days), &(result.hours), &(result.minutes),
        &(result.seconds), &(result.milliseconds), &(result.microseconds),
        &(result.nanoseconds), largest_unit);
    MAYBE_RETURN(maybe_time_difference, Nothing<DurationRecord>());
  }
  // 8. Let date1 be ? CreateTemporalDate(balanceResult.[[Year]],
  // balanceResult.[[Month]], balanceResult.[[Day]], calendar).
  Handle<JSTemporalPlainDate> date1;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, date1,
      CreateTemporalDate(isolate, balanced_year, balanced_month, balanced_day,
                         calendar),
      Nothing<DurationRecord>());
  // 9. Let date2 be ? CreateTemporalDate(y2, mon2, d2, calendar).
  Handle<JSTemporalPlainDate> date2;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, date2, CreateTemporalDate(isolate, y2, mon2, d2, calendar),
      Nothing<DurationRecord>());
  // 10. Let dateLargestUnit be ! LargerOfTwoTemporalUnits("day", largestUnit).
  Unit date_largest_unit =
      LargerOfTwoTemporalUnits(isolate, Unit::kDay, largest_unit);
  // 11. Let untilOptions be ? MergeLargestUnitOption(options, dateLargestUnit).
  Handle<JSObject> until_options;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, until_options,
      MergeLargestUnitOption(isolate, options, date_largest_unit),
      Nothing<DurationRecord>());
  // 12. Let dateDifference be ? CalendarDateUntil(calendar, date1, date2,
  // untilOptions).
  Handle<JSTemporalDuration> date_difference;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, date_difference,
      CalendarDateUntil(isolate, calendar, date1, date2, until_options),
      Nothing<DurationRecord>());
  // 13. Let balanceResult be ? BalanceDuration(dateDifference.[[Days]],
  // timeDifference.[[Hours]], timeDifference.[[Minutes]],
  // timeDifference.[[Seconds]], timeDifference.[[Milliseconds]],
  // timeDifference.[[Microseconds]], timeDifference.[[Nanoseconds]],
  // largestUnit).
  result.days = date_difference->days().Number();
  Maybe<bool> maybe_balance_result = BalanceDuration(
      isolate, &(result.days), &(result.hours), &(result.minutes),
      &(result.seconds), &(result.milliseconds), &(result.microseconds),
      &(result.nanoseconds), largest_unit);
  MAYBE_RETURN(maybe_balance_result, Nothing<DurationRecord>());
  // 14. Return the Record { [[Years]]: dateDifference.[[Years]], [[Months]]:
  // dateDifference.[[Months]], [[Weeks]]: dateDifference.[[Weeks]], [[Days]]:
  // balanceResult.[[Days]], [[Hours]]: balanceResult.[[Hours]], [[Minutes]]:
  // balanceResult.[[Minutes]], [[Seconds]]: balanceResult.[[Seconds]],
  // [[Milliseconds]]: balanceResult.[[Milliseconds]], [[Microseconds]]:
  // balanceResult.[[Microseconds]], [[Nanoseconds]]:
  // balanceResult.[[Nanoseconds]] }.
  result.years = date_difference->years().Number();
  result.months = date_difference->months().Number();
  result.weeks = date_difference->weeks().Number();
  return Just(result);
}

// #sec-temporal-addinstant
MaybeHandle<BigInt> AddInstant(Isolate* isolate,
                               Handle<BigInt> epoch_nanoseconds, int32_t hours,
                               int32_t minutes, int32_t seconds,
                               int32_t milliseconds, int32_t microseconds,
                               int32_t nanoseconds) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: hours, minutes, seconds, milliseconds, microseconds, and
  // nanoseconds are integer Number values.
  // 2. Let result be epochNanoseconds + ℤ(nanoseconds) +
  // ℤ(microseconds) × 1000ℤ + ℤ(milliseconds) × 106ℤ + ℤ(seconds) × 109ℤ +
  // ℤ(minutes) × 60ℤ × 109ℤ + ℤ(hours) × 3600ℤ × 109ℤ.
  Handle<BigInt> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      BigInt::Add(isolate, epoch_nanoseconds,
                  BigInt::FromInt64(isolate, nanoseconds)),
      BigInt);
  Handle<BigInt> temp;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temp,
      BigInt::Multiply(isolate, BigInt::FromInt64(isolate, microseconds),
                       BigInt::FromInt64(isolate, 1000)),
      BigInt);
  ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                             BigInt::Add(isolate, result, temp), BigInt);

  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temp,
      BigInt::Multiply(isolate, BigInt::FromInt64(isolate, milliseconds),
                       BigInt::FromInt64(isolate, 1000000)),
      BigInt);
  ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                             BigInt::Add(isolate, result, temp), BigInt);

  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temp,
      BigInt::Multiply(isolate, BigInt::FromInt64(isolate, seconds),
                       BigInt::FromInt64(isolate, 1000000000)),
      BigInt);
  ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                             BigInt::Add(isolate, result, temp), BigInt);

  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temp,
      BigInt::Multiply(isolate, BigInt::FromInt64(isolate, minutes),
                       BigInt::FromInt64(isolate, 1000000000)),
      BigInt);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temp,
      BigInt::Multiply(isolate, temp, BigInt::FromInt64(isolate, 60)), BigInt);
  ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                             BigInt::Add(isolate, result, temp), BigInt);

  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temp,
      BigInt::Multiply(isolate, BigInt::FromInt64(isolate, hours),
                       BigInt::FromInt64(isolate, 1000000000)),
      BigInt);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temp,
      BigInt::Multiply(isolate, temp, BigInt::FromInt64(isolate, 3600)),
      BigInt);
  ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                             BigInt::Add(isolate, result, temp), BigInt);

  // 3. If ! IsValidEpochNanoseconds(result) is false, throw a RangeError
  // exception.
  if (!IsValidEpochNanoseconds(isolate, result)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), BigInt);
  }
  // 4. Return result.
  return result;
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

MaybeHandle<BigInt> RoundTemporalInstant(Isolate* isolate, Handle<BigInt> ns,
                                         double increment, Unit unit,
                                         RoundingMode rounding_mode) {
  TEMPORAL_ENTER_FUNC

  double factor = increment;
  switch (unit) {
    case Unit::kHour:
      factor *= 3600000000000;
      break;
    case Unit::kMinute:
      factor *= 60000000000;
      break;
    case Unit::kSecond:
      factor *= 1000000000;
      break;
    case Unit::kMillisecond:
      factor *= 1000000;
      break;
    case Unit::kMicrosecond:
      factor *= 1000;
      break;
    case Unit::kNanosecond:
      break;
    default:
      UNREACHABLE();
  }
  return RoundNumberToIncrement(isolate, ns, factor, rounding_mode);
}

// #sec-temporal-calculateoffsetshift
Maybe<int64_t> CalculateOffsetShift(Isolate* isolate,
                                    Handle<Object> relative_to_obj,
                                    const DurationRecord& dur) {
  TEMPORAL_ENTER_FUNC

  // 1. If Type(relativeTo) is not Object or relativeTo does not have an
  // [[InitializedTemporalZonedDateTime]] internal slot, return 0.
  if (!relative_to_obj->IsJSTemporalZonedDateTime()) {
    int64_t zero = 0;
    return Just(zero);
  }
  Handle<JSTemporalZonedDateTime> relative_to =
      Handle<JSTemporalZonedDateTime>::cast(relative_to_obj);
  // 2. Let instant be ! CreateTemporalInstant(relativeTo.[[Nanoseconds]]).
  Handle<BigInt> relative_to_ns =
      Handle<BigInt>(relative_to->nanoseconds(), isolate);
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, instant, CreateTemporalInstant(isolate, relative_to_ns),
      Nothing<int64_t>());
  // 3. Let offsetBefore be ? GetOffsetNanosecondsFor(relativeTo.[[TimeZone]],
  // instant).
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(relative_to->time_zone(), isolate);
  Maybe<int64_t> maybe_offset_before =
      GetOffsetNanosecondsFor(isolate, time_zone, instant);
  MAYBE_RETURN(maybe_offset_before, Nothing<int64_t>());
  // 4. Let after be ? AddZonedDateTime(relativeTo.[[Nanoseconds]],
  // relativeTo.[[TimeZone]], relativeTo.[[Calendar]], y, mon, w, d, h, min, s,
  // ms, mus, ns).
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(relative_to->calendar(), isolate);
  Handle<BigInt> after;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, after,
      AddZonedDateTime(isolate, relative_to_ns, time_zone, calendar, dur),
      Nothing<int64_t>());
  // 5. Let instantAfter be ! CreateTemporalInstant(after).
  Handle<JSTemporalInstant> instant_after;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, instant_after,
                                   CreateTemporalInstant(isolate, after),
                                   Nothing<int64_t>());
  // 6. Let offsetAfter be ? GetOffsetNanosecondsFor(relativeTo.[[TimeZone]],
  // instantAfter).
  Maybe<int64_t> maybe_offset_after =
      GetOffsetNanosecondsFor(isolate, time_zone, instant_after);
  MAYBE_RETURN(maybe_offset_after, Nothing<int64_t>());
  // 7. Return offsetAfter − offsetBefore.
  return Just(maybe_offset_after.FromJust() - maybe_offset_before.FromJust());
}

// #sec-temporal-unbalancedurationrelative
Maybe<bool> BalanceDurationRelative(Isolate* isolate, double* years,
                                    double* months, double* weeks, double* days,
                                    Unit largest_unit,
                                    Handle<Object> relative_to_obj,
                                    const char* method) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  // 1. If largestUnit is not one of "year", "month", or "week", or years,
  // months, weeks, and days are all 0, then
  if ((largest_unit != Unit::kYear && largest_unit != Unit::kMonth &&
       largest_unit != Unit::kWeek) ||
      (*years == 0 && *months == 0 && *weeks == 0 && *days == 0)) {
    // a. Return the Record { [[Years]]: years, [[Months]]: months, [[Weeks]]:
    // weeks, [[Days]]: days }.
    return Just(true);
  }

  // 2. Let sign be ! DurationSign(years, months, weeks, days, 0, 0, 0, 0, 0,
  // 0).
  int32_t sign =
      DurationSign(isolate, {*years, *months, *weeks, *days, 0, 0, 0, 0, 0, 0});

  // 3. Assert: sign ≠ 0.
  CHECK_NE(sign, 0);

  // 4. Let oneYear be ! CreateTemporalDuration(sign, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0).
  Handle<JSTemporalDuration> one_year;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, one_year,
      CreateTemporalDuration(isolate, sign, 0, 0, 0, 0, 0, 0, 0, 0, 0),
      Nothing<bool>());

  // 5. Let oneMonth be ! CreateTemporalDuration(0, sign, 0, 0, 0, 0, 0, 0, 0,
  // 0).
  Handle<JSTemporalDuration> one_month;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, one_month,
      CreateTemporalDuration(isolate, 0, sign, 0, 0, 0, 0, 0, 0, 0, 0),
      Nothing<bool>());

  // 6. Let oneWeek be ! CreateTemporalDuration(0, 0, sign, 0, 0, 0, 0, 0, 0,
  // 0).
  Handle<JSTemporalDuration> one_week;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, one_week,
      CreateTemporalDuration(isolate, 0, 0, sign, 0, 0, 0, 0, 0, 0, 0),
      Nothing<bool>());

  // 7. Set relativeTo to ? ToTemporalDateTime(relativeTo).
  Handle<JSTemporalPlainDateTime> relative_to;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, relative_to,
      ToTemporalDateTime(isolate, relative_to_obj, method), Nothing<bool>());

  // 8. Let calendar be relativeTo.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(relative_to->calendar(), isolate);

  double move_result_days;
  Handle<JSTemporalPlainDateTime> move_result;
  // 9. If largestUnit is "year", then
  if (largest_unit == Unit::kYear) {
    // a. Let moveResult be ? MoveRelativeDate(calendar, relativeTo, oneYear).
    CHECK(relative_to->IsJSTemporalPlainDateTime());
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, move_result,
        MoveRelativeDate(isolate, calendar,
                         Handle<JSTemporalPlainDateTime>::cast(relative_to),
                         one_year, &move_result_days, method),
        Nothing<bool>());

    // b. Set relativeTo to moveResult.[[RelativeTo]].
    relative_to = move_result;

    // c. Let oneYearDays be moveResult.[[Days]].
    double one_year_days = move_result_days;

    // d. Repeat, while abs(days) ≥ abs(oneYearDays),
    while (abs(*days) >= abs(one_year_days)) {
      // i. Set days to days − oneYearDays.
      *days -= one_year_days;

      // ii. Set years to years + sign.
      *years += sign;

      // iii. Set moveResult to ? MoveRelativeDate(calendar, relativeTo,
      // oneYear).
      CHECK(relative_to->IsJSTemporalPlainDateTime());
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar,
                           Handle<JSTemporalPlainDateTime>::cast(relative_to),
                           one_year, &move_result_days, method),
          Nothing<bool>());

      // iv. Set relativeTo to moveResult.[[RelativeTo]].
      relative_to = move_result;

      // v. Set oneYearDays to moveResult.[[Days]].
      one_year_days = move_result_days;
    }
    // e. Let moveResult be ? MoveRelativeDate(calendar, relativeTo, oneMonth).
    CHECK(relative_to->IsJSTemporalPlainDateTime());
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, move_result,
        MoveRelativeDate(isolate, calendar,
                         Handle<JSTemporalPlainDateTime>::cast(relative_to),
                         one_month, &move_result_days, method),
        Nothing<bool>());

    // f. Set relativeTo to moveResult.[[RelativeTo]].
    relative_to = move_result;

    // g. Let oneMonthDays be moveResult.[[Days]].
    double one_month_days = move_result_days;

    // h. Repeat, while abs(days) ≥ abs(oneMonthDays),
    while (abs(*days) >= abs(one_month_days)) {
      // i. Set days to days − oneMonthDays.
      *days -= one_month_days;

      // ii. Set months to months + sign.
      *months += sign;

      // iii. Set moveResult to ? MoveRelativeDate(calendar, relativeTo,
      // oneMonth).
      CHECK(relative_to->IsJSTemporalPlainDateTime());
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar,
                           Handle<JSTemporalPlainDateTime>::cast(relative_to),
                           one_month, &move_result_days, method),
          Nothing<bool>());

      // iv. Set relativeTo to moveResult.[[RelativeTo]].
      relative_to = move_result;

      // v. Set oneMonthDays to moveResult.[[Days]].
      one_month_days = move_result_days;
    }

    // i. Let dateAdd be ? GetMethod(calendar, "dateAdd").
    Handle<Object> date_add;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_add,
        Object::GetMethod(calendar, factory->dateAdd_string()),
        Nothing<bool>());

    // j. Let addOptions be ! OrdinaryObjectCreate(null).
    Handle<JSObject> add_options = factory->NewJSObjectWithNullProto();

    // k. Let newRelativeTo be ? CalendarDateAdd(calendar, relativeTo, oneYear,
    // addOptions, dateAdd).
    Handle<JSTemporalPlainDate> new_relative_to;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, new_relative_to,
        CalendarDateAdd(isolate, calendar, relative_to, one_year, add_options,
                        date_add),
        Nothing<bool>());

    // l. Let dateUntil be ? GetMethod(calendar, "dateUntil").
    Handle<Object> date_until;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_until,
        Object::GetMethod(calendar, factory->dateUntil_string()),
        Nothing<bool>());

    // m. Let untilOptions be ! OrdinaryObjectCreate(null).
    Handle<JSObject> until_options = factory->NewJSObjectWithNullProto();

    // n. Perform ! CreateDataPropertyOrThrow(untilOptions, "largestUnit",
    // "month").
    CHECK(JSReceiver::CreateDataProperty(
              isolate, until_options, factory->largestUnit_string(),
              factory->month_string(), Just(kThrowOnError))
              .FromJust());

    // o. Let untilResult be ? CalendarDateUntil(calendar, relativeTo,
    // newRelativeTo, untilOptions, dateUntil).
    Handle<JSTemporalDuration> until_result;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, until_result,
        CalendarDateUntil(isolate, calendar, relative_to, new_relative_to,
                          until_options, date_until),
        Nothing<bool>());

    // p. Let oneYearMonths be untilResult.[[Months]].
    double one_year_months = until_result->months().Number();

    // Because relative_to is PlainDateTime and new_relative_to is PlainDate
    // We use a relative_to_obj to hold it.
    relative_to_obj = relative_to;
    // q. Repeat, while abs(months) ≥ abs(oneYearMonths),
    while (abs(*months) >= abs(one_year_months)) {
      // i. Set months to months − oneYearMonths.
      *months -= one_year_months;

      // ii. Set years to years + sign.
      *years += sign;

      // iii. Set relativeTo to newRelativeTo.
      relative_to_obj = new_relative_to;
      // iv. Let addOptions be ! OrdinaryObjectCreate(null).
      add_options = factory->NewJSObjectWithNullProto();
      // v. Set newRelativeTo to ? CalendarDateAdd(calendar, relativeTo,
      // oneYear, addOptions, dateAdd).
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, new_relative_to,
          CalendarDateAdd(isolate, calendar, relative_to_obj, one_year,
                          add_options, date_add),
          Nothing<bool>());
      // vi. Let untilOptions be ! OrdinaryObjectCreate(null).
      until_options = factory->NewJSObjectWithNullProto();
      // vii. Perform ! CreateDataPropertyOrThrow(untilOptions, "largestUnit",
      // "month").
      CHECK(JSReceiver::CreateDataProperty(
                isolate, until_options, factory->largestUnit_string(),
                factory->month_string(), Just(kThrowOnError))
                .FromJust());
      // viii. Set untilResult to ? CalendarDateUntil(calendar, relativeTo,
      // newRelativeTo, untilOptions, dateUntil).
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, until_result,
          CalendarDateUntil(isolate, calendar, relative_to_obj, new_relative_to,
                            until_options, date_until),
          Nothing<bool>());

      // ix. Set oneYearMonths to untilResult.[[Months]].
      one_year_months = until_result->months().Number();
    }
    // 10. Else if largestUnit is "month", then
  } else if (largest_unit == Unit::kMonth) {
    // a. Let moveResult be ? MoveRelativeDate(calendar, relativeTo, oneMonth).
    CHECK(relative_to->IsJSTemporalPlainDateTime());
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, move_result,
        MoveRelativeDate(isolate, calendar,
                         Handle<JSTemporalPlainDateTime>::cast(relative_to),
                         one_month, &move_result_days, method),
        Nothing<bool>());

    // b. Set relativeTo to moveResult.[[RelativeTo]].
    relative_to = move_result;

    // c. Let oneMonthDays be moveResult.[[Days]].
    double one_month_days = move_result_days;

    // d. Repeat, while abs(days) ≥ abs(oneMonthDays),
    while (abs(*days) >= abs(one_month_days)) {
      // i. Set days to days − oneMonthDays.
      *days -= one_month_days;

      // ii. Set months to months + sign.
      *months += sign;

      // iii. Set moveResult to ? MoveRelativeDate(calendar, relativeTo,
      // oneMonth).
      CHECK(relative_to->IsJSTemporalPlainDateTime());
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar,
                           Handle<JSTemporalPlainDateTime>::cast(relative_to),
                           one_month, &move_result_days, method),
          Nothing<bool>());

      // iv. Set relativeTo to moveResult.[[RelativeTo]].
      relative_to = move_result;

      // v. Set oneMonthDays to moveResult.[[Days]].
      one_month_days = move_result_days;
    }
    // 11. Else,
  } else {
    // a. Assert: largestUnit is "week".
    CHECK_EQ(largest_unit, Unit::kWeek);
    // b. Let moveResult be ? MoveRelativeDate(calendar, relativeTo, oneWeek).
    CHECK(relative_to->IsJSTemporalPlainDateTime());
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, move_result,
        MoveRelativeDate(isolate, calendar,
                         Handle<JSTemporalPlainDateTime>::cast(relative_to),
                         one_week, &move_result_days, method),
        Nothing<bool>());

    // c. Set relativeTo to moveResult.[[RelativeTo]].
    relative_to = move_result;

    // d. Let oneWeekDays be moveResult.[[Days]].
    double one_week_days = move_result_days;

    // e. Repeat, while abs(days) ≥ abs(oneWeekDays),
    while (abs(*days) >= abs(one_week_days)) {
      // i. Set days to days − oneWeekDays.
      *days -= one_week_days;

      // ii. Set weeks to weeks + sign.
      *weeks += sign;

      // iii. Set moveResult to ? MoveRelativeDate(calendar, relativeTo,
      // oneWeek).
      CHECK(relative_to->IsJSTemporalPlainDateTime());
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar,
                           Handle<JSTemporalPlainDateTime>::cast(relative_to),
                           one_week, &move_result_days, method),
          Nothing<bool>());

      // iv. Set relativeTo to moveResult.[[RelativeTo]].
      relative_to = move_result;

      // v. Set oneWeekDays to moveResult.[[Days]].
      one_week_days = move_result_days;
    }
  }
  // 12. Return the Record { [[Years]]: years, [[Months]]: months, [[Weeks]]:
  // weeks, [[Days]]: days }.
  return Just(true);
}

// #sec-temporal-unbalancedurationrelative
Maybe<bool> UnbalanceDurationRelative(Isolate* isolate, double* years,
                                      double* months, double* weeks,
                                      double* days, Unit largest_unit,
                                      Handle<Object> relative_to,
                                      const char* method) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  // 1. If relativeTo is not present, set relativeTo to undefined.

  // 2. If largestUnit is "year", or years, months, weeks, and days are all 0,
  // then
  if (largest_unit == Unit::kYear ||
      ((*years == 0) && (*months == 0) && (*weeks == 0) && (*days == 0))) {
    // a. Return the new Record { [[Years]]: years, [[Months]]: months,
    // [[Weeks]]: weeks, [[Days]]: days }.
    return Just(true);
  }
  // 3. Let sign be ! DurationSign(years, months, weeks, days, 0, 0, 0, 0, 0,
  // 0).
  int32_t sign =
      DurationSign(isolate, {*years, *months, *weeks, *days, 0, 0, 0, 0, 0, 0});
  // 4. Assert: sign ≠ 0.
  CHECK_NE(sign, 0);
  // 5. Let oneYear be ! CreateTemporalDuration(sign, 0, 0, 0, 0, 0, 0, 0, 0,
  // 0).
  Handle<JSTemporalDuration> one_year;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, one_year,
      CreateTemporalDuration(isolate, sign, 0, 0, 0, 0, 0, 0, 0, 0, 0),
      Nothing<bool>());
  // 6. Let oneMonth be ! CreateTemporalDuration(0, sign, 0, 0, 0, 0, 0, 0, 0,
  // 0).
  Handle<JSTemporalDuration> one_month;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, one_month,
      CreateTemporalDuration(isolate, 0, sign, 0, 0, 0, 0, 0, 0, 0, 0),
      Nothing<bool>());
  // 7. Let oneWeek be ! CreateTemporalDuration(0, 0, sign, 0, 0, 0, 0, 0, 0,
  // 0).
  Handle<JSTemporalDuration> one_week;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, one_week,
      CreateTemporalDuration(isolate, 0, 0, sign, 0, 0, 0, 0, 0, 0, 0),
      Nothing<bool>());
  // 8. If relativeTo is not undefined, then
  Handle<Object> calendar_obj;
  if (!relative_to->IsUndefined()) {
    // a. Set relativeTo to ? ToTemporalDateTime(relativeTo).
    Handle<JSTemporalPlainDateTime> date_time;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_time, ToTemporalDateTime(isolate, relative_to, method),
        Nothing<bool>());
    relative_to = date_time;
    // b. Let calendar be relativeTo.[[Calendar]].
    calendar_obj = Handle<Object>(date_time->calendar(), isolate);
    // 9. Else,
  } else {
    // a. Let calendar be undefined.
    calendar_obj = factory->undefined_value();
  }
  // 10. If largestUnit is "month", then
  if (largest_unit == Unit::kMonth) {
    // a. If calendar is undefined, then
    if (calendar_obj->IsUndefined()) {
      // i. Throw a RangeError exception.
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), Nothing<bool>());
    }
    CHECK(calendar_obj->IsJSReceiver());
    Handle<JSReceiver> calendar = Handle<JSReceiver>::cast(calendar_obj);
    // b. Let dateAdd be ? GetMethod(calendar, "dateAdd").
    Handle<Object> date_add;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_add,
        Object::GetMethod(calendar, factory->dateAdd_string()),
        Nothing<bool>());
    // c. Let dateUntil be ? GetMethod(calendar, "dateUntil").
    Handle<Object> date_until;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, date_until,
        Object::GetMethod(calendar, factory->dateUntil_string()),
        Nothing<bool>());
    // d. Repeat, while years ≠ 0,
    while (*years != 0) {
      // i. Let addOptions be ! OrdinaryObjectCreate(null).
      Handle<JSObject> add_options = factory->NewJSObjectWithNullProto();
      // ii. Let newRelativeTo be ? CalendarDateAdd(calendar, relativeTo,
      // oneYear, addOptions, dateAdd).
      Handle<JSTemporalPlainDate> new_relative_to;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, new_relative_to,
          CalendarDateAdd(isolate, calendar, relative_to, one_year, add_options,
                          date_add),
          Nothing<bool>());
      // iii. Let untilOptions be ! OrdinaryObjectCreate(null).
      Handle<JSObject> until_options = factory->NewJSObjectWithNullProto();
      // iv. Perform ! CreateDataPropertyOrThrow(untilOptions, "largestUnit",
      // "month").
      CHECK(JSReceiver::CreateDataProperty(
                isolate, until_options, factory->largestUnit_string(),
                factory->month_string(), Just(kThrowOnError))
                .FromJust());
      // v. Let untilResult be ? CalendarDateUntil(calendar, relativeTo,
      // newRelativeTo, untilOptions, dateUntil).
      Handle<JSTemporalDuration> until_result;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, until_result,
          CalendarDateUntil(isolate, calendar, relative_to, new_relative_to,
                            until_options, date_until),
          Nothing<bool>());
      // vi. Let oneYearMonths be untilResult.[[Months]].
      double one_year_months = until_result->months().Number();

      // vii. Set relativeTo to newRelativeTo.
      relative_to = new_relative_to;

      // viii. Set years to years − sign.
      *years -= sign;

      // ix. Set months to months + oneYearMonths.
      *months += one_year_months;
    }
    // 11. Else if largestUnit is "week", then
  } else if (largest_unit == Unit::kWeek) {
    // a. If calendar is undefined, then
    if (calendar_obj->IsUndefined()) {
      // i. Throw a RangeError exception.
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), Nothing<bool>());
    }
    CHECK(calendar_obj->IsJSReceiver());
    Handle<JSReceiver> calendar = Handle<JSReceiver>::cast(calendar_obj);
    double move_result_days;
    Handle<JSTemporalPlainDateTime> move_result;
    // b. Repeat, while years ≠ 0,
    while (*years != 0) {
      // i. Let moveResult be ? MoveRelativeDate(calendar, relativeTo, oneYear).
      CHECK(relative_to->IsJSTemporalPlainDateTime());
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar,
                           Handle<JSTemporalPlainDateTime>::cast(relative_to),
                           one_year, &move_result_days, method),
          Nothing<bool>());
      // ii. Set relativeTo to moveResult.[[RelativeTo]].
      relative_to = move_result;
      // iii. Let oneYearDays be moveResult.[[Days]].

      // iv. Set years to years − sign.
      *years -= sign;
      // v. Set days to days + oneYearDays.
      *days += move_result_days;
    }
    // c. Repeat, while months ≠ 0,
    while (*months != 0) {
      // i. Let moveResult be ? MoveRelativeDate(calendar, relativeTo,
      // oneMonth).
      CHECK(relative_to->IsJSTemporalPlainDateTime());
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar,
                           Handle<JSTemporalPlainDateTime>::cast(relative_to),
                           one_month, &move_result_days, method),
          Nothing<bool>());
      // ii. Set relativeTo to moveResult.[[RelativeTo]].
      relative_to = move_result;
      // iii. Let oneMonthDays be moveResult.[[Days]].

      // iv. Set months to months − sign.
      *months -= sign;
      // v. Set days to days + oneMonthDays.
      *days += move_result_days;
    }
    // 12. Else,
  } else {
    // a. If any of years, months, and weeks are not zero, then
    // I believe this is checking on weeks not days.
    // See https://github.com/tc39/proposal-temporal/pull/1728
    if ((*years != 0) || (*months != 0) || (*weeks != 0)) {
      // i. If calendar is undefined, then
      if (calendar_obj->IsUndefined()) {
        // i. Throw a RangeError exception.
        THROW_NEW_ERROR_RETURN_VALUE(
            isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), Nothing<bool>());
      }
      CHECK(calendar_obj->IsJSReceiver());
      Handle<JSReceiver> calendar = Handle<JSReceiver>::cast(calendar_obj);
      double move_result_days;
      Handle<JSTemporalPlainDateTime> move_result;
      // ii. Repeat, while years ≠ 0,
      while (*years != 0) {
        // 1. Let moveResult be ? MoveRelativeDate(calendar, relativeTo,
        // oneYear).
        CHECK(relative_to->IsJSTemporalPlainDateTime());
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, move_result,
            MoveRelativeDate(isolate, calendar,
                             Handle<JSTemporalPlainDateTime>::cast(relative_to),
                             one_year, &move_result_days, method),
            Nothing<bool>());
        // 2. Set relativeTo to moveResult.[[RelativeTo]].
        relative_to = move_result;
        // 3. Let oneYearDays be moveResult.[[Days]].

        // 4. Set years to years − sign.
        *years -= sign;
        // 5. Set days to days + oneYearDays.
        *days += move_result_days;
      }
      // iii. Repeat, while months ≠ 0,
      while (*months != 0) {
        // 1. Let moveResult be ? MoveRelativeDate(calendar, relativeTo,
        // oneMonth).
        CHECK(relative_to->IsJSTemporalPlainDateTime());
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, move_result,
            MoveRelativeDate(isolate, calendar,
                             Handle<JSTemporalPlainDateTime>::cast(relative_to),
                             one_month, &move_result_days, method),
            Nothing<bool>());
        // 2. Set relativeTo to moveResult.[[RelativeTo]].
        relative_to = move_result;
        // 3. Let oneMonthDays be moveResult.[[Days]].

        // 4. Set months to months − sign.
        *months -= sign;
        // 5. Set days to days + oneMonthDays.
        *days += move_result_days;
      }
      // iv. Repeat, while weeks ≠ 0,
      while (*weeks != 0) {
        // 1. Let moveResult be ? MoveRelativeDate(calendar, relativeTo,
        // oneWeek).
        CHECK(relative_to->IsJSTemporalPlainDateTime());
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, move_result,
            MoveRelativeDate(isolate, calendar,
                             Handle<JSTemporalPlainDateTime>::cast(relative_to),
                             one_week, &move_result_days, method),
            Nothing<bool>());
        // 2. Set relativeTo to moveResult.[[RelativeTo]].
        relative_to = move_result;
        // 3. Let oneWeekDays be moveResult.[[Days]].

        // 4. Set weeks to weeks − sign.
        *weeks -= sign;
        // 5. Set days to days + oneWeekDays.
        *days += move_result_days;
      }
    }
  }
  // 13. Return the new Record { [[Years]]: years, [[Months]]: months,
  // [[Weeks]]: weeks, [[Days]]: days }.
  return Just(true);
}

// #sec-temporal-roundduration
Maybe<DurationRecord> RoundDuration(Isolate* isolate, const DurationRecord& dur,
                                    double increment, Unit unit,
                                    RoundingMode rounding_mode,
                                    double* remainder, const char* method) {
  TEMPORAL_ENTER_FUNC

  // 1. If relativeTo is not present, set relativeTo to undefined.
  return RoundDuration(isolate, dur, increment, unit, rounding_mode,
                       isolate->factory()->undefined_value(), remainder,
                       method);
}

DateTimeRecordCommon RoundTime(Isolate* isolate, int32_t hour, int32_t minute,
                               int32_t second, int32_t millisecond,
                               int32_t microsecond, int32_t nanosecond,
                               double increment, Unit unit,
                               RoundingMode rounding_mode,
                               double day_length_ns) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: hour, minute, second, millisecond, microsecond, nanosecond, and
  // increment are integers.
  // 2. Let fractionalSecond be nanosecond × 10−9 + microsecond × 10−6 +
  // millisecond × 10−3 + second.
  double fractional_second = nanosecond / 100000000.0 +
                             microsecond / 1000000.0 + millisecond / 1000.0 +
                             second;
  double quantity;
  switch (unit) {
    // 3. If unit is "day", then
    case Unit::kDay:
      // a. If dayLengthNs is not present, set it to 8.64 × 1013.
      // b. Let quantity be (((((hour × 60 + minute) × 60 + second) × 1000 +
      // millisecond) × 1000 + microsecond) × 1000 + nanosecond) / dayLengthNs.
      quantity =
          (((((hour * 60.0 + minute) * 60.0 + second) * 1000.0 + millisecond) *
                1000.0 +
            microsecond) *
               1000.0 +
           nanosecond) /
          day_length_ns;
      break;
    // 4. Else if unit is "hour", then
    case Unit::kHour:
      // a. Let quantity be (fractionalSecond / 60 + minute) / 60 + hour.
      quantity = (fractional_second / 60.0 + minute) / 60.0 + hour;
      break;
    // 5. Else if unit is "minute", then
    case Unit::kMinute:
      // a. Let quantity be fractionalSecond / 60 + minute.
      quantity = fractional_second / 60.0 + minute;
      break;
    // 6. Else if unit is "second", then
    case Unit::kSecond:
      // a. Let quantity be fractionalSecond.
      quantity = fractional_second;
      break;
    // 7. Else if unit is "millisecond", then
    case Unit::kMillisecond:
      // a. Let quantity be nanosecond × 10−6 + microsecond × 10−3 +
      // millisecond.
      quantity = nanosecond / 1000000.0 + microsecond / 1000.0 + millisecond;
      break;
    // 8. Else if unit is "microsecond", then
    case Unit::kMicrosecond:
      // a. Let quantity be nanosecond × 10−3 + microsecond.
      quantity = nanosecond / 1000.0 + microsecond;
      break;
    // 9. Else,
    default:
      // a. Assert: unit is "nanosecond".
      CHECK_EQ(unit, Unit::kNanosecond);
      // b. Let quantity be nanosecond.
      quantity = nanosecond;
      break;
  }
  // 10. Let result be ! RoundNumberToIncrement(quantity, increment,
  // roundingMode).
  int32_t result =
      RoundNumberToIncrement(isolate, quantity, increment, rounding_mode);

  switch (unit) {
    // 11. If unit is "day", then
    case Unit::kDay:
      // a. Return the Record { [[Days]]: result, [[Hour]]: 0, [[Minute]]: 0,
      // [[Second]]: 0, [[Millisecond]]: 0, [[Microsecond]]: 0, [[Nanosecond]]:
      // 0 }.
      return {0, 0, result, 0, 0, 0, 0, 0, 0};
    // 12. If unit is "hour", then
    case Unit::kHour:
      // a. Return ! BalanceTime(result, 0, 0, 0, 0, 0).
      return BalanceTime(isolate, result, 0, 0, 0, 0, 0);
    // 13. If unit is "minute", then
    case Unit::kMinute:
      // a. Return ! BalanceTime(hour, result, 0, 0, 0, 0).
      return BalanceTime(isolate, hour, result, 0, 0, 0, 0);
    // 14. If unit is "second", then
    case Unit::kSecond:
      // a. Return ! BalanceTime(hour, minute, result, 0, 0, 0).
      return BalanceTime(isolate, hour, minute, result, 0, 0, 0);
    // 15. If unit is "millisecond", then
    case Unit::kMillisecond:
      // a. Return ! BalanceTime(hour, minute, second, result, 0, 0).
      return BalanceTime(isolate, hour, minute, second, result, 0, 0);
    // 16. If unit is "microsecond", then
    case Unit::kMicrosecond:
      // a. Return ! BalanceTime(hour, minute, second, millisecond, result, 0).
      return BalanceTime(isolate, hour, minute, second, millisecond, result, 0);
    default:
      // 17. Assert: unit is "nanosecond".
      CHECK_EQ(unit, Unit::kNanosecond);
      // 18. Return ! BalanceTime(hour, minute, second, millisecond,
      // microsecond, result).
      return BalanceTime(isolate, hour, minute, second, millisecond,
                         microsecond, result);
  }
}

DateTimeRecordCommon RoundTime(Isolate* isolate, int32_t hour, int32_t minute,
                               int32_t second, int32_t millisecond,
                               int32_t microsecond, int32_t nanosecond,
                               double increment, Unit unit,
                               RoundingMode rounding_mode) {
  TEMPORAL_ENTER_FUNC

  // 3-a. If dayLengthNs is not present, set it to 8.64 × 1013.
  return RoundTime(isolate, hour, minute, second, millisecond, microsecond,
                   nanosecond, increment, unit, rounding_mode, 86400000000000);
}

// #sec-temporal-roundisodatetime
DateTimeRecordCommon RoundISODateTime(Isolate* isolate, int32_t year,
                                      int32_t month, int32_t day, int32_t hour,
                                      int32_t minute, int32_t second,
                                      int32_t millisecond, int32_t microsecond,
                                      int32_t nanosecond, double increment,
                                      Unit unit, RoundingMode rounding_mode,
                                      double day_length_ns) {
  TEMPORAL_ENTER_FUNC

  // 3. Let roundedTime be ! RoundTime(hour, minute, second, millisecond,
  // microsecond, nanosecond, increment, unit, roundingMode, dayLength).
  DateTimeRecordCommon ret =
      RoundTime(isolate, hour, minute, second, millisecond, microsecond,
                nanosecond, increment, unit, rounding_mode, day_length_ns);
  // 4. Let balanceResult be ! BalanceISODate(year, month, day +
  // roundedTime.[[Days]]).
  ret.year = year;
  ret.month = month;
  ret.day += day;
  BalanceISODate(isolate, &(ret.year), &(ret.month), &(ret.day));
  // 5. Return the Record { [[Year]]: balanceResult.[[Year]], [[Month]]:
  // balanceResult.[[Month]], [[Day]]: balanceResult.[[Day]], [[Hour]]:
  // roundedTime.[[Hour]], [[Minute]]: roundedTime.[[Minute]], [[Second]]:
  // roundedTime.[[Second]], [[Millisecond]]: roundedTime.[[Millisecond]],
  // [[Microsecond]]: roundedTime.[[Microsecond]], [[Nanosecond]]:
  // roundedTime.[[Nanosecond]] }.
  return ret;
}
DateTimeRecordCommon RoundISODateTime(Isolate* isolate, int32_t year,
                                      int32_t month, int32_t day, int32_t hour,
                                      int32_t minute, int32_t second,
                                      int32_t millisecond, int32_t microsecond,
                                      int32_t nanosecond, double increment,
                                      Unit unit, RoundingMode rounding_mode) {
  TEMPORAL_ENTER_FUNC

  // 2. If dayLength is not present, set dayLength to 8.64 × 1013.
  return RoundISODateTime(isolate, year, month, day, hour, minute, second,
                          millisecond, microsecond, nanosecond, increment, unit,
                          rounding_mode, 86400000000000);
}

// #sec-temporal-moverelativezoneddatetime
MaybeHandle<JSTemporalZonedDateTime> MoveRelativeZonedDateTime(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    double years, double months, double weeks, double days,
    const char* method) {
  TEMPORAL_ENTER_FUNC

  // 1. Let intermediateNs be ? AddZonedDateTime(zonedDateTime.[[Nanoseconds]],
  // zonedDateTime.[[TimeZone]], zonedDateTime.[[Calendar]], years, months,
  // weeks, days, 0, 0, 0, 0, 0, 0).
  Handle<BigInt> intermediate_ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, intermediate_ns,
      AddZonedDateTime(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate),
          Handle<JSReceiver>(zoned_date_time->time_zone(), isolate),
          Handle<JSReceiver>(zoned_date_time->calendar(), isolate),
          {years, months, weeks, days, 0, 0, 0, 0, 0, 0}),
      JSTemporalZonedDateTime);
  // 2. Return ! CreateTemporalZonedDateTime(intermediateNs,
  // zonedDateTime.[[TimeZone]], zonedDateTime.[[Calendar]]).
  return CreateTemporalZonedDateTime(
      isolate, intermediate_ns,
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate),
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate));
}

Maybe<DurationRecord> RoundDuration(Isolate* isolate,
                                    const DurationRecord& duration,
                                    double increment, Unit unit,
                                    RoundingMode rounding_mode,
                                    Handle<Object> relative_to,
                                    double* remainder, const char* method) {
  TEMPORAL_ENTER_FUNC

  Factory* factory = isolate->factory();
  DurationRecord dur = duration;
  // 1. If relativeTo is not present, set relativeTo to undefined.
  // 2. Let years, months, weeks, days, hours, minutes, seconds, milliseconds,
  // microseconds, nanoseconds, and increment each be the mathematical values of
  // themselves.
  // 3. If unit is "year", "month", or "week", and relativeTo is undefined, then
  if ((unit == Unit::kYear || unit == Unit::kMonth || unit == Unit::kWeek) &&
      relative_to->IsUndefined()) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                                 Nothing<DurationRecord>());
  }

  // 4. Let zonedRelativeTo be undefined.
  Handle<Object> zoned_relative_to = isolate->factory()->undefined_value();

  Handle<JSReceiver> calendar;
  // 5. If relativeTo is not undefined, then
  if (!relative_to->IsUndefined()) {
    // a. If relativeTo has an [[InitializedTemporalZonedDateTime]] internal
    // slot, then
    if (relative_to->IsJSTemporalZonedDateTime()) {
      // i. Let instant be ! CreateTemporalInstant(relativeTo.[[Nanoseconds]]).
      Handle<BigInt> relative_to_nanoseconds = Handle<BigInt>(
          Handle<JSTemporalZonedDateTime>::cast(relative_to)->nanoseconds(),
          isolate);
      Handle<JSTemporalInstant> instant;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, instant,
          CreateTemporalInstant(isolate, relative_to_nanoseconds),
          Nothing<DurationRecord>());
      // ii. Set zonedRelativeTo to relativeTo.
      zoned_relative_to = relative_to;
      // iii. Set relativeTo to ?
      // BuiltinTimeZoneGetPlainDateTimeFor(relativeTo.[[TimeZone]], instant,
      // relativeTo.[[Calendar]]).
      Handle<JSReceiver> relative_to_time_zone = Handle<JSReceiver>(
          Handle<JSTemporalZonedDateTime>::cast(relative_to)->time_zone(),
          isolate);
      Handle<JSReceiver> relative_to_calendar = Handle<JSReceiver>(
          Handle<JSTemporalZonedDateTime>::cast(relative_to)->calendar(),
          isolate);

      Handle<JSTemporalPlainDateTime> created_date_time;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, created_date_time,
          BuiltinTimeZoneGetPlainDateTimeFor(isolate, relative_to_time_zone,
                                             instant, relative_to_calendar),
          Nothing<DurationRecord>());
      relative_to = created_date_time;
      // b. Else,
    } else {
      // i. Assert: relativeTo has an [[InitializedTemporalDateTime]] internal
      // slot.
      CHECK(relative_to->IsJSTemporalPlainDateTime());
    }
    // c. Let calendar be relativeTo.[[Calendar]].
    CHECK(relative_to->IsJSTemporalPlainDateTime());
    calendar = Handle<JSReceiver>(
        Handle<JSTemporalPlainDateTime>::cast(relative_to)->calendar(),
        isolate);
  }
  double fractional_seconds;
  // 6. If unit is one of "year", "month", "week", or "day", then
  if (unit == Unit::kYear || unit == Unit::kMonth || unit == Unit::kWeek ||
      unit == Unit::kDay) {
    // a. Let nanoseconds be ! TotalDurationNanoseconds(0, hours, minutes,
    // seconds, milliseconds, microseconds, nanoseconds, 0).
    dur.nanoseconds = TotalDurationNanoseconds(
        isolate, 0, dur.hours, dur.minutes, dur.seconds, dur.milliseconds,
        dur.microseconds, dur.nanoseconds, 0);

    // b. Let intermediate be undefined.
    Handle<Object> intermediate = isolate->factory()->undefined_value();

    // c. If zonedRelativeTo is not undefined, then
    if (!zoned_relative_to->IsUndefined()) {
      CHECK(zoned_relative_to->IsJSTemporalZonedDateTime());
      // i. Let intermediate be ? MoveRelativeZonedDateTime(zonedRelativeTo,
      // years, months, weeks, days).
      Handle<JSTemporalZonedDateTime> zoned_date_time;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, zoned_date_time,
          MoveRelativeZonedDateTime(
              isolate, Handle<JSTemporalZonedDateTime>::cast(zoned_relative_to),
              dur.years, dur.months, dur.weeks, dur.days, method),
          Nothing<DurationRecord>());
      intermediate = zoned_date_time;
    }

    // d. Let result be ? NanosecondsToDays(nanoseconds, intermediate).
    double result_days;
    double result_nanoseconds;
    double result_day_length;
    Maybe<bool> maybe_result =
        NanosecondsToDays(isolate, dur.nanoseconds, intermediate, &result_days,
                          &result_nanoseconds, &result_day_length);
    MAYBE_RETURN(maybe_result, Nothing<DurationRecord>());

    // e. Set days to days + result.[[Days]] + result.[[Nanoseconds]] /
    // result.[[DayLength]].
    dur.days += result_days + result_nanoseconds / result_day_length;

    // f. Set hours, minutes, seconds, milliseconds, microseconds, and
    // nanoseconds to 0.
    dur.hours = dur.minutes = dur.seconds = dur.milliseconds =
        dur.microseconds = dur.nanoseconds = 0;

    // 7. Else,
  } else {
    // a. Let fractionalSeconds be nanoseconds × 10−9 + microseconds × 10−6 +
    // milliseconds × 10−3 + seconds.
    fractional_seconds = dur.nanoseconds / 1000000000.0 +
                         dur.microseconds / 1000000.0 +
                         dur.milliseconds / 1000.0 + dur.seconds;
  }
  // 8. Let remainder be undefined.
  *remainder = 0;

  switch (unit) {
    // 9. If unit is "year", then
    case Unit::kYear: {
      // a. Let yearsDuration be ? CreateTemporalDuration(years, 0, 0, 0, 0, 0,
      // 0, 0, 0, 0).
      Handle<JSTemporalDuration> years_duration;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_duration,
          CreateTemporalDuration(isolate, dur.years, 0, 0, 0, 0, 0, 0, 0, 0, 0),
          Nothing<DurationRecord>());

      // b. Let dateAdd be ? GetMethod(calendar, "dateAdd").
      Handle<Object> date_add;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, date_add,
          Object::GetMethod(calendar, factory->dateAdd_string()),
          Nothing<DurationRecord>());

      // c. Let firstAddOptions be ! OrdinaryObjectCreate(null).
      Handle<JSObject> first_add_options = factory->NewJSObjectWithNullProto();

      // d. Let yearsLater be ? CalendarDateAdd(calendar, relativeTo,
      // yearsDuration, firstAddOptions, dateAdd).
      Handle<JSTemporalPlainDate> years_later;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_later,
          CalendarDateAdd(isolate, calendar, relative_to, years_duration,
                          first_add_options, date_add),
          Nothing<DurationRecord>());

      // e. Let yearsMonthsWeeks be ? CreateTemporalDuration(years, months,
      // weeks, 0, 0, 0, 0, 0, 0, 0).
      Handle<JSTemporalDuration> years_months_weeks;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_months_weeks,
          CreateTemporalDuration(isolate, dur.years, dur.months, dur.weeks, 0,
                                 0, 0, 0, 0, 0, 0),
          Nothing<DurationRecord>());

      // f. Let secondAddOptions be ! OrdinaryObjectCreate(null).
      Handle<JSObject> second_add_options = factory->NewJSObjectWithNullProto();

      // g. Let yearsMonthsWeeksLater be ? CalendarDateAdd(calendar, relativeTo,
      // yearsMonthsWeeks, secondAddOptions, dateAdd).
      Handle<JSTemporalPlainDate> years_months_weeks_later;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_months_weeks_later,
          CalendarDateAdd(isolate, calendar, relative_to, years_months_weeks,
                          second_add_options, date_add),
          Nothing<DurationRecord>());

      // h. Let monthsWeeksInDays be ? DaysUntil(yearsLater,
      // yearsMonthsWeeksLater).
      Maybe<double> maybe_months_weeks_in_days =
          DaysUntil(isolate, years_later, years_months_weeks_later, method);
      MAYBE_RETURN(maybe_months_weeks_in_days, Nothing<DurationRecord>());
      double months_weeks_in_days = maybe_months_weeks_in_days.FromJust();

      // i. Set relativeTo to yearsLater.
      relative_to = years_later;

      // j. Let days be days + monthsWeeksInDays.
      dur.days += months_weeks_in_days;

      // k. Let daysDuration be ? CreateTemporalDuration(0, 0, 0, days, 0, 0, 0,
      // 0, 0, 0).
      Handle<JSTemporalDuration> days_duration;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, days_duration,
          CreateTemporalDuration(isolate, 0, 0, 0, dur.days, 0, 0, 0, 0, 0, 0),
          Nothing<DurationRecord>());

      // l. Let thirdAddOptions be ! OrdinaryObjectCreate(null).
      Handle<JSObject> third_add_options = factory->NewJSObjectWithNullProto();

      // m. Let daysLater be ? CalendarDateAdd(calendar, relativeTo,
      // daysDuration, thirdAddOptions, dateAdd).
      Handle<JSTemporalPlainDate> days_later;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, days_later,
          CalendarDateAdd(isolate, calendar, relative_to, days_duration,
                          third_add_options, date_add),
          Nothing<DurationRecord>());

      // n. Let untilOptions be ! OrdinaryObjectCreate(null).
      Handle<JSObject> until_options = factory->NewJSObjectWithNullProto();

      // o. Perform ! CreateDataPropertyOrThrow(untilOptions, "largestUnit",
      // "year").
      CHECK(JSReceiver::CreateDataProperty(
                isolate, until_options, factory->largestUnit_string(),
                factory->year_string(), Just(kThrowOnError))
                .FromJust());

      // p. Let timePassed be ? CalendarDateUntil(calendar, relativeTo,
      // daysLater, untilOptions).
      Handle<JSTemporalDuration> time_passed;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, time_passed,
          CalendarDateUntil(isolate, calendar, relative_to, days_later,
                            until_options),
          Nothing<DurationRecord>());

      // q. Let yearsPassed be timePassed.[[Years]].
      double years_passed = time_passed->years().Number();

      // r. Set years to years + yearsPassed.
      dur.years += years_passed;

      // s. Let oldRelativeTo be relativeTo.
      Handle<Object> old_relative_to = relative_to;

      // t. Let yearsDuration be ? CreateTemporalDuration(yearsPassed, 0, 0, 0,
      // 0, 0, 0, 0, 0, 0).
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_duration,
          CreateTemporalDuration(isolate, years_passed, 0, 0, 0, 0, 0, 0, 0, 0,
                                 0),
          Nothing<DurationRecord>());

      // u. Let fourthAddOptions be ! OrdinaryObjectCreate(null).
      Handle<JSObject> fourth_add_options = factory->NewJSObjectWithNullProto();

      // v. Set relativeTo to ? CalendarDateAdd(calendar, relativeTo,
      // yearsDuration, fourthAddOptions, dateAdd).
      Handle<JSTemporalPlainDate> relative_to_date;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, relative_to_date,
          CalendarDateAdd(isolate, calendar, relative_to, years_duration,
                          fourth_add_options, date_add),
          Nothing<DurationRecord>());

      // w. Let daysPassed be ? DaysUntil(oldRelativeTo, relativeTo).
      Maybe<double> maybe_days_passed =
          DaysUntil(isolate, old_relative_to, relative_to, method);
      MAYBE_RETURN(maybe_days_passed, Nothing<DurationRecord>());
      double days_passed = maybe_days_passed.FromJust();

      // x. Set days to days - daysPassed.
      dur.days -= days_passed;

      // y. Let sign be ! Sign(days).
      // z. If sign is 0, set sign to 1.
      double sign = dur.days >= 0 ? 1 : -1;

      // ab. Let oneYear be ? CreateTemporalDuration(sign, 0, 0, 0, 0, 0, 0, 0,
      // 0, 0).
      Handle<JSTemporalDuration> one_year;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, one_year,
          CreateTemporalDuration(isolate, sign, 0, 0, 0, 0, 0, 0, 0, 0, 0),
          Nothing<DurationRecord>());

      // ac Set _relativeTo_ to !
      // CreateTemporalDateTime(_relativeTo_.[[ISOYear]],
      // _relativeTo_.[[ISOMonth]], _relativeTo_.[[ISODay]], 0, 0, 0, 0, 0, 0,
      // _relativeTo_.[[Calendar]]).
      Handle<JSReceiver> relative_to_date_calendar =
          Handle<JSReceiver>(relative_to_date->calendar(), isolate);
      Handle<JSTemporalPlainDateTime> relative_to_date_time;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, relative_to_date_time,
          CreateTemporalDateTime(isolate, relative_to_date->iso_year(),
                                 relative_to_date->iso_month(),
                                 relative_to_date->iso_day(), 0, 0, 0, 0, 0, 0,
                                 relative_to_date_calendar),
          Nothing<DurationRecord>());
      relative_to = relative_to_date_time;

      // ac. Let moveResult be ? MoveRelativeDate(calendar, relativeTo,
      // oneYear).
      Handle<JSTemporalPlainDateTime> move_result;
      double one_year_days;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar, relative_to_date_time, one_year,
                           &one_year_days, method),
          Nothing<DurationRecord>());

      // ad. Let oneYearDays be moveResult.[[Days]].
      // ae. Let fractionalYears be years + days / abs(oneYearDays).
      double fractional_years = dur.years + dur.days / abs(one_year_days);

      // af. Set years to ! RoundNumberToIncrement(fractionalYears, increment,
      // roundingMode).
      dur.years = RoundNumberToIncrement(isolate, fractional_years, increment,
                                         rounding_mode);

      // ah. Set remainder to fractionalYears - years.
      *remainder = fractional_years - dur.years;

      // ai. Set months, weeks, and days to 0.
      dur.months = dur.weeks = dur.days = 0;
    } break;
    // 10. Else if unit is "month", then
    case Unit::kMonth: {
      // a. Let yearsMonths be ? CreateTemporalDuration(years, months, 0, 0, 0,
      // 0, 0, 0, 0, 0).
      Handle<JSTemporalDuration> years_months;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_months,
          CreateTemporalDuration(isolate, dur.years, dur.months, 0, 0, 0, 0, 0,
                                 0, 0, 0),
          Nothing<DurationRecord>());

      // b. Let dateAdd be ? GetMethod(calendar, "dateAdd").
      Handle<Object> date_add;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, date_add,
          Object::GetMethod(calendar, factory->dateAdd_string()),
          Nothing<DurationRecord>());

      // c. Let firstAddOptions be ! OrdinaryObjectCreate(null).
      Handle<JSObject> first_add_options = factory->NewJSObjectWithNullProto();

      // d. Let yearsMonthsLater be ? CalendarDateAdd(calendar, relativeTo,
      // yearsMonths, firstAddOptions, dateAdd).
      Handle<JSTemporalPlainDate> years_months_later;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_months_later,
          CalendarDateAdd(isolate, calendar, relative_to, years_months,
                          first_add_options, date_add),
          Nothing<DurationRecord>());

      // e. Let yearsMonthsWeeks be ? CreateTemporalDuration(years, months,
      // weeks, 0, 0, 0, 0, 0, 0, 0).
      Handle<JSTemporalDuration> years_months_weeks;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_months_weeks,
          CreateTemporalDuration(isolate, dur.years, dur.months, dur.weeks, 0,
                                 0, 0, 0, 0, 0, 0),
          Nothing<DurationRecord>());

      // f. Let secondAddOptions be ! OrdinaryObjectCreate(null).
      Handle<JSObject> second_add_options = factory->NewJSObjectWithNullProto();

      // g. Let yearsMonthsWeeksLater be ? CalendarDateAdd(calendar, relativeTo,
      // yearsMonthsWeeks, secondAddOptions, dateAdd).
      Handle<JSTemporalPlainDate> years_months_weeks_later;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, years_months_weeks_later,
          CalendarDateAdd(isolate, calendar, relative_to, years_months_weeks,
                          second_add_options, date_add),
          Nothing<DurationRecord>());

      // h. Let weeksInDays be ? DaysUntil(yearsMonthsLater,
      // yearsMonthsWeeksLater).
      Maybe<double> maybe_weeks_in_days = DaysUntil(
          isolate, years_months_later, years_months_weeks_later, method);
      MAYBE_RETURN(maybe_weeks_in_days, Nothing<DurationRecord>());
      double weeks_in_days = maybe_weeks_in_days.FromJust();

      // i. Set relativeTo to yearsMonthsLater.

      // j. Let days be days + weeksInDays.
      dur.days += weeks_in_days;

      // k. Let sign be ! Sign(days).
      // l. If sign is 0, set sign to 1.
      double sign = dur.days >= 0 ? 1 : -1;

      // m. Let oneMonth be ? CreateTemporalDuration(0, sign, 0, 0, 0, 0, 0, 0,
      // 0, 0).
      Handle<JSTemporalDuration> one_month;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, one_month,
          CreateTemporalDuration(isolate, 0, sign, 0, 0, 0, 0, 0, 0, 0, 0),
          Nothing<DurationRecord>());

      // n. Set relativeTo to ! CreateTemporalDateTime(relativeTo.[[ISOYear]],
      // relativeTo.[[ISOMonth]], relativeTo.[[ISODay]], 0, 0, 0, 0, 0, 0,
      // relativeTo.[[Calendar]]).
      Handle<JSReceiver> years_months_later_calendar =
          Handle<JSReceiver>(years_months_later->calendar(), isolate);
      Handle<JSTemporalPlainDateTime> relative_to_date_time;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, relative_to_date_time,
          CreateTemporalDateTime(isolate, years_months_later->iso_year(),
                                 years_months_later->iso_month(),
                                 years_months_later->iso_day(), 0, 0, 0, 0, 0,
                                 0, years_months_later_calendar),
          Nothing<DurationRecord>());

      // n. Let moveResult be ? MoveRelativeDate(calendar, relativeTo,
      // oneMonth).
      Handle<JSTemporalPlainDateTime> move_result;
      double one_month_days;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar, relative_to_date_time, one_month,
                           &one_month_days, method),
          Nothing<DurationRecord>());

      // o. Set relativeTo to moveResult.[[RelativeTo]].
      relative_to = move_result;

      // p. Let oneMonthDays be moveResult.[[Days]].

      // q. Repeat, while abs(days) ≥ abs(oneMonthDays),
      while (abs(dur.days) >= abs(one_month_days)) {
        // i. Set months to months + sign.
        dur.months += sign;

        // ii. Set days to days − oneMonthDays.
        dur.days -= one_month_days;

        // iii. Set moveResult to ? MoveRelativeDate(calendar, relativeTo,
        // oneMonth).
        CHECK(relative_to->IsJSTemporalPlainDateTime());
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, move_result,
            MoveRelativeDate(isolate, calendar,
                             Handle<JSTemporalPlainDateTime>::cast(relative_to),
                             one_month, &one_month_days, method),
            Nothing<DurationRecord>());

        // iv. Set relativeTo to moveResult.[[RelativeTo]].
        // v. Set oneMonthDays to moveResult.[[Days]].
      }
      // r. Let fractionalMonths be months + days / abs(oneMonthDays).
      double fractional_months =
          dur.months + dur.days / std::abs(one_month_days);

      // s. Set months to ! RoundNumberToIncrement(fractionalMonths, increment,
      // roundingMode).
      dur.months = RoundNumberToIncrement(isolate, fractional_months, increment,
                                          rounding_mode);

      // t. Set remainder to fractionalMonths - months.
      *remainder = fractional_months - dur.months;

      // u. Set weeks and days to 0.
      dur.weeks = dur.days = 0;
    } break;
    // 11. Else if unit is "week", then
    case Unit::kWeek: {
      // a. Let sign be ! Sign(days).
      // b. If sign is 0, set sign to 1.
      int32_t sign = dur.days >= 0 ? 1 : -1;

      // c. Let oneWeek be ? CreateTemporalDuration(0, 0, sign, 0, 0, 0, 0, 0,
      // 0, 0).
      Handle<JSTemporalDuration> one_week;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, one_week,
          CreateTemporalDuration(isolate, 0, 0, sign, 0, 0, 0, 0, 0, 0, 0),
          Nothing<DurationRecord>());

      // d. Let moveResult be ? MoveRelativeDate(calendar, relativeTo, oneWeek).
      CHECK(relative_to->IsJSTemporalPlainDateTime());
      double one_week_days;
      Handle<JSTemporalPlainDateTime> move_result;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, move_result,
          MoveRelativeDate(isolate, calendar,
                           Handle<JSTemporalPlainDateTime>::cast(relative_to),
                           one_week, &one_week_days, method),
          Nothing<DurationRecord>());

      // e. Set relativeTo to moveResult.[[RelativeTo]].
      relative_to = move_result;

      // f. Let oneWeekDays be moveResult.[[Days]].

      // g. Repeat, while abs(days) ≥ abs(oneWeekDays),
      while (std::abs(dur.days) >= std::abs(one_week_days)) {
        // i. Set weeks to weeks + sign.
        dur.weeks += sign;

        // ii. Set days to days − oneWeekDays.
        dur.days -= one_week_days;

        // iii. Set moveResult to ? MoveRelativeDate(calendar, relativeTo,
        // oneWeek).
        CHECK(relative_to->IsJSTemporalPlainDateTime());
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, move_result,
            MoveRelativeDate(isolate, calendar,
                             Handle<JSTemporalPlainDateTime>::cast(relative_to),
                             one_week, &one_week_days, method),
            Nothing<DurationRecord>());

        // iv. Set relativeTo to moveResult.[[RelativeTo]].
        relative_to = move_result;
        // v. Set oneWeekDays to moveResult.[[Days]].
      }

      // h. Let fractionalWeeks be weeks + days / abs(oneWeekDays).
      double fractional_weeks = dur.weeks + dur.days / std::abs(one_week_days);

      // i. Set weeks to ! RoundNumberToIncrement(fractionalWeeks, increment,
      // roundingMode).
      dur.weeks = RoundNumberToIncrement(isolate, fractional_weeks, increment,
                                         rounding_mode);

      // j. Set remainder to fractionalWeeks - weeks.
      *remainder = fractional_weeks - dur.weeks;

      // k. Set days to 0.
      dur.days = 0;
    } break;
    // 12. Else if unit is "day", then
    case Unit::kDay: {
      // a. Let fractionalDays be days.
      double fractional_days = dur.days;

      // b. Set days to ! RoundNumberToIncrement(days, increment, roundingMode).
      dur.days =
          RoundNumberToIncrement(isolate, dur.days, increment, rounding_mode);

      // c. Set remainder to fractionalDays - days.
      *remainder = fractional_days - dur.days;
    } break;
    // 13. Else if unit is "hour", then
    case Unit::kHour: {
      // a. Let fractionalHours be (fractionalSeconds / 60 + minutes) / 60 +
      // hours.
      double fractional_hours =
          (fractional_seconds / 60.0 + dur.minutes) / 60.0 + dur.hours;

      // b. Set hours to ! RoundNumberToIncrement(fractionalHours, increment,
      // roundingMode).
      dur.hours = RoundNumberToIncrement(isolate, fractional_hours, increment,
                                         rounding_mode);

      // c. Set remainder to fractionalHours - hours.
      *remainder = fractional_hours - dur.hours;

      // d. Set minutes, seconds, milliseconds, microseconds, and nanoseconds to
      // 0.
      dur.minutes = dur.seconds = dur.milliseconds = dur.microseconds =
          dur.nanoseconds = 0;
    } break;
    // 14. Else if unit is "minute", then
    case Unit::kMinute: {
      // a. Let fractionalMinutes be fractionalSeconds / 60 + minutes.
      double fractional_minutes = fractional_seconds / 60.0 + dur.minutes;

      // b. Set minutes to ! RoundNumberToIncrement(fractionalMinutes,
      // increment, roundingMode).
      dur.minutes = RoundNumberToIncrement(isolate, fractional_minutes,
                                           increment, rounding_mode);

      // c. Set remainder to fractionalMinutes - minutes.
      *remainder = fractional_minutes - dur.minutes;

      // d. Set seconds, milliseconds, microseconds, and nanoseconds to 0.
      dur.seconds = dur.milliseconds = dur.microseconds = dur.nanoseconds = 0;
    } break;
    // 15. Else if unit is "second", then
    case Unit::kSecond: {
      // a. Set seconds to ! RoundNumberToIncrement(fractionalSeconds,
      // increment, roundingMode).
      dur.seconds = RoundNumberToIncrement(isolate, fractional_seconds,
                                           increment, rounding_mode);

      // b. Set remainder to fractionalSeconds - seconds.
      *remainder = fractional_seconds - dur.seconds;

      // c. Set milliseconds, microseconds, and nanoseconds to 0.
      dur.milliseconds = dur.microseconds = dur.nanoseconds = 0;
    } break;
    // 16. Else if unit is "millisecond", then
    case Unit::kMillisecond: {
      // a. Let fractionalMilliseconds be nanoseconds × 10−6 + microseconds ×
      // 10−3 + milliseconds.
      double fractional_milliseconds = dur.nanoseconds / 1000000.0 +
                                       dur.microseconds / 1000.0 +
                                       dur.milliseconds;

      // b. Set milliseconds to ! RoundNumberToIncrement(fractionalMilliseconds,
      // increment, roundingMode).
      dur.milliseconds = RoundNumberToIncrement(
          isolate, fractional_milliseconds, increment, rounding_mode);

      // c. Set remainder to fractionalMilliseconds - milliseconds.
      *remainder = fractional_milliseconds - dur.milliseconds;

      // d. Set microseconds and nanoseconds to 0.
      dur.microseconds = dur.nanoseconds = 0;
    } break;
    // 17. Else if unit is "microsecond", then
    case Unit::kMicrosecond: {
      // a. Let fractionalMicroseconds be nanoseconds × 10−3 + microseconds.
      double fractional_microseconds =
          dur.nanoseconds / 1000.0 + dur.microseconds;

      // b. Set microseconds to ! RoundNumberToIncrement(fractionalMicroseconds,
      // increment, roundingMode).
      dur.microseconds = RoundNumberToIncrement(
          isolate, fractional_microseconds, increment, rounding_mode);

      // c. Set remainder to fractionalMicroseconds - microseconds.
      *remainder = fractional_microseconds - dur.microseconds;

      // d. Set nanoseconds to 0.
      dur.nanoseconds = 0;
    } break;
    // 18. Else,
    default: {
      // a. Assert: unit is "nanosecond".
      CHECK_EQ(unit, Unit::kNanosecond);
      // b. Set remainder to nanoseconds.
      *remainder = dur.nanoseconds;

      // c. Set nanoseconds to ! RoundNumberToIncrement(nanoseconds, increment,
      // roundingMode).
      dur.nanoseconds = RoundNumberToIncrement(isolate, dur.nanoseconds,
                                               increment, rounding_mode);

      // d. Set remainder to remainder − nanoseconds.
      *remainder -= dur.nanoseconds;
    } break;
  }
  return Just(dur);
}

double RoundHalfAwayFromZero(double x) { return std::round(x); }
double RoundNumberToIncrement(Isolate* isolate, double x, double increment,
                              RoundingMode rounding_mode) {
  TEMPORAL_ENTER_FUNC

  // 3. Let quotient be x / increment.
  double quotient = x / increment;
  double rounded;
  switch (rounding_mode) {
    // 4. If roundingMode is "ceil", then
    case RoundingMode::kCeil:
      // a. Let rounded be −floor(−quotient).
      rounded = -std::floor(-quotient);
      break;
    // 5. Else if roundingMode is "floor", then
    case RoundingMode::kFloor:
      // a. Let rounded be floor(quotient).
      rounded = std::floor(quotient);
      break;
    // 6. Else if roundingMode is "trunc", then
    case RoundingMode::kTrunc:
      // a. Let rounded be the integral part of quotient, removing any
      // fractional digits.
      rounded = (quotient > 0) ? std::floor(quotient) : -std::floor(-quotient);
      break;
      // 7. Else,
    default:
      // a. Let rounded be ! RoundHalfAwayFromZero(quotient).
      rounded = RoundHalfAwayFromZero(quotient);
      break;
  }
  // 8. Return rounded × increment.
  return rounded * increment;
}

MaybeHandle<BigInt> RoundNumberToIncrement(Isolate* isolate, Handle<BigInt> x,
                                           double increment,
                                           RoundingMode rounding_mode) {
  TEMPORAL_ENTER_FUNC

  double res =
      RoundNumberToIncrement(isolate, x->AsInt64(), increment, rounding_mode);
  return BigInt::FromNumber(isolate, isolate->factory()->NewNumber(res));
}

// #sec-temporal-interpretisodatetimeoffset
MaybeHandle<BigInt> InterpretISODateTimeOffset(
    Isolate* isolate, double year, double month, double day, double hour,
    double minute, double second, double millisecond, double microsecond,
    double nanosecond, OffsetBehaviour offset_behaviour,
    int64_t offset_nanoseconds, Handle<JSReceiver> time_zone,
    Disambiguation disambiguation, Offset offset_option, const char* method) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: offsetNanoseconds is an integer or undefined.
  // 2. Let calendar be ! GetISO8601Calendar().
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             temporal::GetISO8601Calendar(isolate), BigInt);

  // 3. Let dateTime be ? CreateTemporalDateTime(year, month, day, hour, minute,
  // second, millisecond, microsecond, nanosecond, calendar).
  Handle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      CreateTemporalDateTime(isolate, year, month, day, hour, minute, second,
                             millisecond, microsecond, nanosecond, calendar),
      BigInt);

  // 4. If offsetBehaviour is wall, or offsetOption is "ignore", then
  if (offset_behaviour == OffsetBehaviour::kWall ||
      offset_option == Offset::kIgnore) {
    // a. Let instant be ? BuiltinTimeZoneGetInstantFor(timeZone, dateTime,
    // disambiguation).
    Handle<JSTemporalInstant> instant;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, instant,
        BuiltinTimeZoneGetInstantFor(isolate, time_zone, date_time,
                                     disambiguation),
        BigInt);
    // b. Return instant.[[Nanoseconds]].
    return Handle<BigInt>(instant->nanoseconds(), isolate);
  }
  // 5. If offsetBehaviour is exact, or offsetOption is "use", then
  if (offset_behaviour == OffsetBehaviour::kExact ||
      offset_option == Offset::kUse) {
    // a. Let epochNanoseconds be ? GetEpochFromISOParts(year, month, day, hour,
    // minute, second, millisecond, microsecond, nanosecond).
    Handle<BigInt> epoch_nanoseconds;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, epoch_nanoseconds,
        GetEpochFromISOParts(isolate, year, month, day, hour, minute, second,
                             millisecond, microsecond, nanosecond),
        BigInt);

    // b. Return epochNanoseconds − offsetNanoseconds.
    return BigInt::Subtract(isolate, epoch_nanoseconds,
                            BigInt::FromUint64(isolate, offset_nanoseconds));
  }
  // 6. Assert: offsetBehaviour is option.
  CHECK_EQ(offset_behaviour, OffsetBehaviour::kOption);
  // 7. Assert: offsetOption is "prefer" or "reject".
  CHECK(offset_option == Offset::kPrefer || offset_option == Offset::kReject);
  // 8. Let possibleInstants be ? GetPossibleInstantsFor(timeZone, dateTime).
  Handle<FixedArray> possible_instants;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, possible_instants,
      GetPossibleInstantsFor(isolate, time_zone, date_time), BigInt);

  // 9. For each element candidate of possibleInstants, do
  for (int i = 0; i < possible_instants->length(); i++) {
    Handle<Object> candidate_obj =
        Handle<Object>(possible_instants->get(i), isolate);
    CHECK(candidate_obj->IsJSTemporalInstant());

    Handle<JSTemporalInstant> candidate =
        Handle<JSTemporalInstant>::cast(candidate_obj);
    // a. If ? GetOffsetNanosecondsFor(timeZone, candidate) = offsetNanoseconds,
    // then
    Maybe<int64_t> maybe_candidate_nanoseconds =
        GetOffsetNanosecondsFor(isolate, time_zone, candidate);
    MAYBE_RETURN(maybe_candidate_nanoseconds, Handle<BigInt>());
    if (maybe_candidate_nanoseconds.FromJust() == offset_nanoseconds) {
      // i. Return candidate.[[Nanoseconds]].
      return Handle<BigInt>(candidate->nanoseconds(), isolate);
    }
  }
  // 10. If offsetOption is "reject", throw a RangeError exception.
  if (offset_option == Offset::kReject) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), BigInt);
  }
  // 11. Let instant be ? DisambiguatePossibleInstants(possibleInstants,
  // timeZone, dateTime, disambiguation).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      DisambiguatePossibleInstants(isolate, possible_instants, time_zone,
                                   date_time, disambiguation),
      BigInt);
  // 12. Return instant.[[Nanoseconds]].
  return Handle<BigInt>(instant->nanoseconds(), isolate);
}

MaybeHandle<BigInt> GetEpochFromISOParts(Isolate* isolate, int32_t year,
                                         int32_t month, int32_t day,
                                         int32_t hour, int32_t minute,
                                         int32_t second, int32_t millisecond,
                                         int32_t microsecond,
                                         int32_t nanosecond) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year, month, day, hour, minute, second, millisecond,
  // microsecond, and nanosecond are integers.
  // 2. Assert: ! IsValidISODate(year, month, day) is true.
  CHECK(IsValidISODate(isolate, year, month, day));
  // 3. Assert: ! IsValidTime(hour, minute, second, millisecond, microsecond,
  // nanosecond) is true.
  CHECK(IsValidTime(isolate, hour, minute, second, millisecond, microsecond,
                    nanosecond));
  // 4. Let date be ! MakeDay(𝔽(year), 𝔽(month − 1), 𝔽(day)).
  double date = MakeDay(year, month - 1, day);
  // 5. Let time be ! MakeTime(𝔽(hour), 𝔽(minute), 𝔽(second), 𝔽(millisecond)).
  double time = MakeTime(hour, minute, second, millisecond);
  // 6. Let ms be ! MakeDate(date, time).
  double ms = MakeDate(date, time);
  // 7. Assert: ms is finite.
  // 8. Return ℝ(ms) × 106 + microsecond × 103 + nanosecond.
  Handle<BigInt> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      BigInt::FromNumber(isolate, isolate->factory()->NewNumber(ms)), BigInt);

  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      BigInt::Multiply(isolate, result, BigInt::FromInt64(isolate, 1000000)),
      BigInt);

  Handle<BigInt> temp;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temp,
      BigInt::Multiply(isolate, BigInt::FromInt64(isolate, microsecond),
                       BigInt::FromInt64(isolate, 1000)),
      BigInt);

  ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                             BigInt::Add(isolate, result, temp), BigInt);
  return BigInt::Add(isolate, result, BigInt::FromInt64(isolate, nanosecond));
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

// #sec-temporal-toisodayofweek
int32_t ToISODayOfWeek(Isolate* isolate, int32_t year, int32_t month,
                       int32_t day) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year is an integer.
  // 2. Assert: month is an integer.
  // 3. Assert: day is an integer.
  // 4. Let date be the date given by year, month, and day.
  // 5. Return date's day of the week according to ISO-8601.
  int32_t weekday = isolate->date_cache()->Weekday(
      isolate->date_cache()->DaysFromYearMonth(year, month - 1) + day - 1);
  return weekday == 0 ? 7 : weekday;
}

// #sec-temporal-toisodayofyear
int32_t ToISODayOfYear(Isolate* isolate, int32_t year, int32_t month,
                       int32_t day) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year is an integer.
  // 2. Assert: month is an integer.
  // 3. Assert: day is an integer.
  // 4. Let date be the date given by year, month, and day.
  // 5. Return date's ordinal date in the year according to ISO-8601.
  return day + isolate->date_cache()->DaysFromYearMonth(year, month - 1) -
         isolate->date_cache()->DaysFromYearMonth(year, 0);
}

// #sec-temporal-toisoweekofyear
int32_t ToISOWeekOfYear(Isolate* isolate, int32_t year, int32_t month,
                        int32_t day) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: year is an integer.
  // 2. Assert: month is an integer.
  // 3. Assert: day is an integer.
  // 4. Let date be the date given by year, month, and day.
  // 5. Return date's week number according to ISO-8601.
  int32_t first_day_in_month =
      isolate->date_cache()->DaysFromYearMonth(year, month - 1);
  int32_t weekday =
      isolate->date_cache()->Weekday(first_day_in_month + day - 1);
  int32_t w = (10 + day + first_day_in_month -
               isolate->date_cache()->DaysFromYearMonth(year, 0) -
               (weekday == 0 ? 7 : weekday)) /
              7;
  int32_t p = (year + year / 4 - year / 100 + year / 400) % 7;
  int32_t p1 =
      (year - 1 + (year - 1) / 4 - (year - 1) / 100 + (year - 1) / 400) % 7;
  int32_t p2 =
      (year - 2 + (year - 2) / 4 - (year - 2) / 100 + (year - 2) / 400) % 7;
  int32_t weeks = 52 + ((p == 4 || p1 == 3) ? 1 : 0);
  int32_t weeks1 = 52 + ((p1 == 4 || p2 == 3) ? 1 : 0);
  return (w < 1) ? weeks1 : ((w > weeks) ? 1 : w);
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

bool IsValidISOMonth(Isolate* isolate, int32_t month) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: month is an integer.
  // 2. If month < 1 or month > 12, then
  // a. Return false.
  // 3. Return true.
  return 1 <= month && month <= 12;
}

// #sec-temporal-compareisodate
int32_t CompareISODate(Isolate* isolate, int32_t y1, int32_t m1, int32_t d1,
                       int32_t y2, int32_t m2, int32_t d2) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: y1, m1, d1, y2, m2, and d2 are integers.
  // 2. If y1 > y2, return 1.
  if (y1 > y2) return 1;
  // 3. If y1 < y2, return -1.
  if (y1 < y2) return -1;
  // 4. If m1 > m2, return 1.
  if (m1 > m2) return 1;
  // 5. If m1 < m2, return -1.
  if (m1 < m2) return -1;
  // 6. If d1 > d2, return 1.
  if (d1 > d2) return 1;
  // 7. If d1 < d2, return -1.
  if (d1 < d2) return -1;
  // 8. Return 0.
  return 0;
}

// #sec-temporal-comparetemporaltime
int32_t CompareTemporalTime(Isolate* isolate, int32_t h1, int32_t min1,
                            int32_t s1, int32_t ms1, int32_t mus1, int32_t ns1,
                            int32_t h2, int32_t min2, int32_t s2, int32_t ms2,
                            int32_t mus2, int32_t ns2) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: h1, min1, s1, ms1, mus1, ns1, h2, min2, s2, ms2, mus2, and ns2
  // are integers.
  // 2. If h1 > h2, return 1.
  if (h1 > h2) return 1;
  // 3. If h1 < h2, return -1.
  if (h1 < h2) return -1;
  // 4. If min1 > min2, return 1.
  if (min1 > min2) return 1;
  // 5. If min1 < min2, return -1.
  if (min1 < min2) return -1;
  // 6. If s1 > s2, return 1.
  if (s1 > s2) return 1;
  // 7. If s1 < s2, return -1.
  if (s1 < s2) return -1;
  // 8. If ms1 > ms2, return 1.
  if (ms1 > ms2) return 1;
  // 9. If ms1 < ms2, return -1.
  if (ms1 < ms2) return -1;
  // 10. If mus1 > mus2, return 1.
  if (mus1 > mus2) return 1;
  // 11. If mus1 < mus2, return -1.
  if (mus1 < mus2) return -1;
  // 12. If ns1 > ns2, return 1.
  if (ns1 > ns2) return 1;
  // 13. If ns1 < ns2, return -1.
  if (ns1 < ns2) return -1;
  // 14. Return 0.
  return 0;
}

// #sec-temporal-compareisodatetime
int32_t CompareISODateTime(Isolate* isolate, int32_t y1, int32_t mon1,
                           int32_t d1, int32_t h1, int32_t min1, int32_t s1,
                           int32_t ms1, int32_t mus1, int32_t ns1, int32_t y2,
                           int32_t mon2, int32_t d2, int32_t h2, int32_t min2,
                           int32_t s2, int32_t ms2, int32_t mus2, int32_t ns2) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: y1, mon1, d1, h1, min1, s1, ms1, mus1, ns1, y2, mon2, d2, h2,
  // min2, s2, ms2, mus2, and ns2 are integers.
  // 2. Let dateResult be ! CompareISODate(y1, mon1, d1, y2, mon2, d2).
  int32_t date_result = CompareISODate(isolate, y1, mon1, d1, y2, mon2, d2);
  // 3. If dateResult is not 0, then
  if (date_result != 0) {
    // a. Return dateResult.
    return date_result;
  }
  // 4. Return ! CompareTemporalTime(h1, min1, s1, ms1, mus1, ns1, h2, min2, s2,
  // ms2, mus2, ns2).
  return CompareTemporalTime(isolate, h1, min1, s1, ms1, mus1, ns1, h2, min2,
                             s2, ms2, mus2, ns2);
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

// #sec-temporal-differencetime
DurationRecord DifferenceTime(Isolate* isolate, int32_t h1, int32_t min1,
                              int32_t s1, int32_t ms1, int32_t mus1,
                              int32_t ns1, int32_t h2, int32_t min2, int32_t s2,
                              int32_t ms2, int32_t mus2, int32_t ns2) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: h1, min1, s1, ms1, mus1, ns1, h2, min2, s2, ms2, mus2, and ns2
  // are integers.
  DurationRecord dur;
  // 2. Let hours be h2 − h1.
  dur.hours = h2 - h1;
  // 3. Let minutes be min2 − min1.
  dur.minutes = min2 - min1;
  // 4. Let seconds be s2 − s1.
  dur.seconds = s2 - s1;
  // 5. Let milliseconds be ms2 − ms1.
  dur.milliseconds = ms2 - ms1;
  // 6. Let microseconds be mus2 − mus1.
  dur.microseconds = mus2 - mus1;
  // 7. Let nanoseconds be ns2 − ns1.
  dur.nanoseconds = ns2 - ns1;
  // 8. Let sign be ! DurationSign(0, 0, 0, 0, hours, minutes, seconds,
  // milliseconds, microseconds, nanoseconds).
  double sign = DurationSign(isolate, dur);
  // 9. Let bt be ! BalanceTime(hours, minutes, seconds, milliseconds,
  // microseconds, nanoseconds).
  DateTimeRecordCommon bt =
      BalanceTime(isolate, dur.hours, dur.minutes, dur.seconds,
                  dur.milliseconds, dur.microseconds, dur.nanoseconds);
  // 10. Return the new Record { [[Days]]: bt.[[Days]] × sign, [[Hours]]:
  // bt.[[Hour]] × sign, [[Minutes]]: bt.[[Minute]] × sign, [[Seconds]]:
  // bt.[[Second]] × sign, [[Milliseconds]]: bt.[[Millisecond]] × sign,
  // [[Microseconds]]: bt.[[Microsecond]] × sign, [[Nanoseconds]]:
  // bt.[[Nanosecond]] × sign }.
  return {0,
          0,
          0,
          bt.day * sign,
          bt.hour * sign,
          bt.minute * sign,
          bt.second * sign,
          bt.millisecond * sign,
          bt.microsecond * sign,
          bt.nanosecond * sign};
}

// #sec-temporal-addtime
DateTimeRecordCommon AddTime(Isolate* isolate, int32_t hour, int32_t minute,
                             int32_t second, int32_t millisecond,
                             int32_t microsecond, int32_t nanosecond,
                             int32_t hours, int32_t minutes, int32_t seconds,
                             int32_t milliseconds, int32_t microseconds,
                             int32_t nanoseconds) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: hour, minute, second, millisecond, microsecond, nanosecond,
  // hours, minutes, seconds, milliseconds, microseconds, and nanoseconds are
  // integers.
  // 2. Let hour be hour + hours.
  return BalanceTime(isolate, hour + hours,
                     // 3. Let minute be minute + minutes.
                     minute + minutes,
                     // 4. Let second be second + seconds.
                     second + seconds,
                     // 5. Let millisecond be millisecond + milliseconds.
                     millisecond + milliseconds,
                     // 6. Let microsecond be microsecond + microseconds.
                     microsecond + microseconds,
                     // 7. Let nanosecond be nanosecond + nanoseconds.
                     nanosecond + nanoseconds);
  // 8. Return ! BalanceTime(hour, minute, second, millisecond, microsecond,
  // nanosecond).
}

// #sec-temporal-totaldurationnanoseconds
double TotalDurationNanoseconds(Isolate* isolate, double days, double hours,
                                double minutes, double seconds,
                                double milliseconds, double microseconds,
                                double nanoseconds, int64_t offset_shift) {
  TEMPORAL_ENTER_FUNC

  // 1. Assert: offsetShift is an integer.
  // 2. Set nanoseconds to ℝ(nanoseconds).
  nanoseconds = R(nanoseconds);
  // 3. If days ≠ 0, then
  if (days != 0) {
    // a. Set nanoseconds to nanoseconds − offsetShift.
    nanoseconds -= offset_shift;
  }

  // 4. Set hours to ℝ(hours) + ℝ(days) × 24.
  hours = R(hours) + R(days) * 24;

  // 5. Set minutes to ℝ(minutes) + hours × 60.
  minutes = R(minutes) + hours * 60;

  // 6. Set seconds to ℝ(seconds) + minutes × 60.
  seconds = R(seconds) + minutes * 60;

  // 7. Set milliseconds to ℝ(milliseconds) + seconds × 1000.
  milliseconds = R(milliseconds) + seconds * 1000;

  // 8. Set microseconds to ℝ(microseconds) + milliseconds × 1000.
  microseconds = R(microseconds) + milliseconds * 1000;

  // 9. Return nanoseconds + microseconds × 1000.
  nanoseconds = nanoseconds + microseconds * 1000;
  return nanoseconds;
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

// #sec-temporal.duration.from
MaybeHandle<JSTemporalDuration> JSTemporalDuration::From(Isolate* isolate,
                                                         Handle<Object> item) {
  const char* method = "Temporal.Duration.from";
  //  1. If Type(item) is Object and item has an [[InitializedTemporalDuration]]
  //  internal slot, then
  if (item->IsJSTemporalDuration()) {
    // a. Return ? CreateTemporalDuration(item.[[Years]], item.[[Months]],
    // item.[[Weeks]], item.[[Days]], item.[[Hours]], item.[[Minutes]],
    // item.[[Seconds]], item.[[Milliseconds]], item.[[Microseconds]],
    // item.[[Nanoseconds]]).
    Handle<JSTemporalDuration> duration =
        Handle<JSTemporalDuration>::cast(item);
    return temporal::CreateTemporalDuration(
        isolate, duration->years().Number(), duration->months().Number(),
        duration->weeks().Number(), duration->days().Number(),
        duration->hours().Number(), duration->minutes().Number(),
        duration->seconds().Number(), duration->milliseconds().Number(),
        duration->microseconds().Number(), duration->nanoseconds().Number());
  }
  // 2. Return ? ToTemporalDuration(item).
  return temporal::ToTemporalDuration(isolate, item, method);
}

// #sec-temporal.duration.compare
MaybeHandle<Smi> JSTemporalDuration::Compare(Isolate* isolate,
                                             Handle<Object> one_obj,
                                             Handle<Object> two_obj,
                                             Handle<Object> options_obj) {
  const char* method = "Temporal.Duration.compare";
  // 1. Set one to ? ToTemporalDuration(one).
  Handle<JSTemporalDuration> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one, temporal::ToTemporalDuration(isolate, one_obj, method),
      Smi);
  // 2. Set two to ? ToTemporalDuration(two).
  Handle<JSTemporalDuration> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two, temporal::ToTemporalDuration(isolate, two_obj, method),
      Smi);
  // 3. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method), Smi);
  // 4. Let relativeTo be ? ToRelativeTemporalObject(options).
  Handle<Object> relative_to;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, relative_to,
      temporal::ToRelativeTemporalObject(isolate, options, method), Smi);
  // 5. Let shift1 be ! CalculateOffsetShift(relativeTo, one.[[Years]],
  // one.[[Months]], one.[[Weeks]], one.[[Days]], one.[[Hours]],
  // one.[[Minutes]], one.[[Seconds]], one.[[Milliseconds]],
  // one.[[Microseconds]], one.[[Nanoseconds]]).
  Maybe<int64_t> maybe_shift1 = temporal::CalculateOffsetShift(
      isolate, relative_to,
      {one->years().Number(), one->months().Number(), one->weeks().Number(),
       one->days().Number(), one->hours().Number(), one->minutes().Number(),
       one->seconds().Number(), one->milliseconds().Number(),
       one->microseconds().Number(), one->nanoseconds().Number()});
  MAYBE_RETURN(maybe_shift1, Handle<Smi>());
  int64_t shift1 = maybe_shift1.FromJust();
  // 6. Let shift2 be ! CalculateOffsetShift(relativeTo, two.[[Years]],
  // two.[[Months]], two.[[Weeks]], two.[[Days]], two.[[Hours]],
  // two.[[Minutes]], two.[[Seconds]], two.[[Milliseconds]],
  // two.[[Microseconds]], two.[[Nanoseconds]]).
  Maybe<int64_t> maybe_shift2 = temporal::CalculateOffsetShift(
      isolate, relative_to,
      {two->years().Number(), two->months().Number(), two->weeks().Number(),
       two->days().Number(), two->hours().Number(), two->minutes().Number(),
       two->seconds().Number(), two->milliseconds().Number(),
       two->microseconds().Number(), two->nanoseconds().Number()});
  MAYBE_RETURN(maybe_shift2, Handle<Smi>());
  int64_t shift2 = maybe_shift2.FromJust();
  double days1;
  double days2;
  // 7. If any of one.[[Years]], two.[[Years]], one.[[Months]], two.[[Months]],
  // one.[[Weeks]], or two.[[Weeks]] are not 0, then
  if (!(one->years().IsZero() && two->years().IsZero() &&
        one->months().IsZero() && two->months().IsZero() &&
        one->weeks().IsZero() && two->weeks().IsZero())) {
    // a. Let unbalanceResult1 be ? UnbalanceDurationRelative(one.[[Years]],
    // one.[[Months]], one.[[Weeks]], one.[[Days]], "day", relativeTo).
    double years = one->years().Number();
    double months = one->months().Number();
    double weeks = one->weeks().Number();
    days1 = one->days().Number();
    Maybe<bool> maybe_unbalance_result1 = temporal::UnbalanceDurationRelative(
        isolate, &years, &months, &weeks, &days1, temporal::Unit::kDay,
        relative_to, method);
    MAYBE_RETURN(maybe_unbalance_result1, Handle<Smi>());
    CHECK(maybe_unbalance_result1.FromJust());
    // b. Let unbalanceResult2 be ? UnbalanceDurationRelative(two.[[Years]],
    // two.[[Months]], two.[[Weeks]], two.[[Days]], "day", relativeTo).
    years = two->years().Number();
    months = two->months().Number();
    weeks = two->weeks().Number();
    days2 = two->days().Number();
    Maybe<bool> maybe_unbalance_result2 = temporal::UnbalanceDurationRelative(
        isolate, &years, &months, &weeks, &days2, temporal::Unit::kDay,
        relative_to, method);
    MAYBE_RETURN(maybe_unbalance_result2, Handle<Smi>());
    CHECK(maybe_unbalance_result2.FromJust());
    // c. Let days1 be unbalanceResult1.[[Days]].
    // d. Let days2 be unbalanceResult2.[[Days]].
    // 8. Else,
  } else {
    // a. Let days1 be one.[[Days]].
    days1 = one->days().Number();
    // b. Let days2 be two.[[Days]].
    days2 = two->days().Number();
  }
  // 9. Let ns1 be ! TotalDurationNanoseconds(days1, one.[[Hours]],
  // one.[[Minutes]], one.[[Seconds]], one.[[Milliseconds]],
  // one.[[Microseconds]], one.[[Nanoseconds]], shift1).
  double ns1 = temporal::TotalDurationNanoseconds(
      isolate, days1, one->hours().Number(), one->minutes().Number(),
      one->seconds().Number(), one->milliseconds().Number(),
      one->microseconds().Number(), one->nanoseconds().Number(), shift1);
  // 10. Let ns2 be ! TotalDurationNanoseconds(days2, two.[[Hours]],
  // two.[[Minutes]], two.[[Seconds]], two.[[Milliseconds]],
  // two.[[Microseconds]], two.[[Nanoseconds]], shift2).
  double ns2 = temporal::TotalDurationNanoseconds(
      isolate, days2, two->hours().Number(), two->minutes().Number(),
      two->seconds().Number(), two->milliseconds().Number(),
      two->microseconds().Number(), two->nanoseconds().Number(), shift2);
  // 10. Let ns2 be ! TotalDurationNanoseconds(days2, two.[[Hours]],
  // two.[[Minutes]], two.[[Seconds]], two.[[Milliseconds]],
  // two.[[Microseconds]], two.[[Nanoseconds]], shift2).
  // 11. If ns1 > ns2, return 1.
  if (ns1 > ns2) {
    return Handle<Smi>(Smi::FromInt(1), isolate);
  }
  // 12. If ns1 < ns2, return −1.
  if (ns1 < ns2) {
    return Handle<Smi>(Smi::FromInt(-1), isolate);
  }
  // 13. Return 0.
  return Handle<Smi>(Smi::FromInt(0), isolate);
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

// #sec-temporal.duration.prototype.with
MaybeHandle<JSTemporalDuration> JSTemporalDuration::With(
    Isolate* isolate, Handle<JSTemporalDuration> duration,
    Handle<Object> temporal_duration_like) {
  Factory* factory = isolate->factory();
  if (!temporal_duration_like->IsJSReceiver()) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalDuration);
  }
  Handle<JSReceiver> receiver =
      Handle<JSReceiver>::cast(temporal_duration_like);
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Let temporalDurationLike be ? ToPartialDuration(temporalDurationLike).
  // ToPartialDuration
  // 3. Let any be false.
  bool any = false;
  // 4. For each row of Table 7, except the header row, in table order, do
  // a. Let property be the Property value of the current row.
  // b. Let value be ? Get(temporalDurationLike, property).
  // c. If value is not undefined, then
  // i. Set any to true.
  // ii. Set value to ? ToNumber(value).
  // iii. If !IsIntegerNumber(value) is false, then
  // 1. Throw a RangeError exception
  // iv. Set result's internal slot whose name is the Internal Slot value of
  // the current row to value.
  // 4. If temporalDurationLike.[[Years]] is not undefined, then
  // a . Let years be temporalDurationLike.[[Years]].
  // 5. Else,
  // a. Let years be duration.[[Years]].
#define GET_PROP(name)                                                        \
  double name = duration->name().Number();                                    \
  {                                                                           \
    Handle<Object> value;                                                     \
    ASSIGN_RETURN_ON_EXCEPTION(                                               \
        isolate, value,                                                       \
        JSReceiver::GetProperty(isolate, receiver, factory->name##_string()), \
        JSTemporalDuration);                                                  \
    if (!value->IsUndefined()) {                                              \
      Handle<Object> number;                                                  \
      ASSIGN_RETURN_ON_EXCEPTION(isolate, number,                             \
                                 Object::ToNumber(isolate, value),            \
                                 JSTemporalDuration);                         \
      name = number->Number();                                                \
      if (name - std::floor(name) != 0) {                                     \
        THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),        \
                        JSTemporalDuration);                                  \
      }                                                                       \
      any = true;                                                             \
    }                                                                         \
  }

  GET_PROP(years);
  GET_PROP(months);
  GET_PROP(weeks);
  GET_PROP(days);
  GET_PROP(hours);
  GET_PROP(minutes);
  GET_PROP(seconds);
  GET_PROP(milliseconds);
  GET_PROP(microseconds);
  GET_PROP(nanoseconds);
#undef GET_PROP

  // 5. If any is false, then
  if (!any) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalDuration);
  }

  // 24. Return ? CreateTemporalDuration(years, months, weeks, days, hours,
  // minutes, seconds, milliseconds, microseconds, nanoseconds).
  return temporal::CreateTemporalDuration(isolate, years, months, weeks, days,
                                          hours, minutes, seconds, milliseconds,
                                          microseconds, nanoseconds);
}

// #sec-temporal.duration.prototype.negated
MaybeHandle<JSTemporalDuration> JSTemporalDuration::Negated(
    Isolate* isolate, Handle<JSTemporalDuration> duration) {
  // Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).

  // 3. Return ! CreateNegatedTemporalDuration(duration).
  return temporal::CreateNegatedTemporalDuration(isolate, duration);
}

// #sec-temporal.duration.prototype.abs
MaybeHandle<JSTemporalDuration> JSTemporalDuration::Abs(
    Isolate* isolate, Handle<JSTemporalDuration> duration) {
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Return ? CreateTemporalDuration(abs(duration.[[Years]]),
  // abs(duration.[[Months]]), abs(duration.[[Weeks]]), abs(duration.[[Days]]),
  // abs(duration.[[Hours]]), abs(duration.[[Minutes]]),
  // abs(duration.[[Seconds]]), abs(duration.[[Milliseconds]]),
  // abs(duration.[[Microseconds]]), abs(duration.[[Nanoseconds]])).
  return temporal::CreateTemporalDuration(
      isolate, std::abs(duration->years().Number()),
      std::abs(duration->months().Number()),
      std::abs(duration->weeks().Number()), std::abs(duration->days().Number()),
      std::abs(duration->hours().Number()),
      std::abs(duration->minutes().Number()),
      std::abs(duration->seconds().Number()),
      std::abs(duration->milliseconds().Number()),
      std::abs(duration->microseconds().Number()),
      std::abs(duration->nanoseconds().Number()));
}

namespace {
MaybeHandle<Object> MoveRelativeZonedDateTime(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    double years, double months, double weeks, double days) {
  // 1. Let intermediateNs be ? AddZonedDateTime(zonedDateTime.[[Nanoseconds]],
  // zonedDateTime.[[TimeZone]], zonedDateTime.[[Calendar]], years, months,
  // weeks, days, 0, 0, 0, 0, 0, 0).
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  Handle<BigInt> intermediate_ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, intermediate_ns,
      temporal::AddZonedDateTime(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate),
          time_zone, calendar, {years, months, weeks, days, 0, 0, 0, 0, 0, 0}),
      Object);

  // 2. Return ? CreateTemporalZonedDateTime(intermediateNs,
  // zonedDateTime.[[TimeZone]], zonedDateTime.[[Calendar]]).
  return temporal::CreateTemporalZonedDateTime(isolate, intermediate_ns,
                                               time_zone, calendar);
}

MaybeHandle<JSTemporalDuration> AddOrSubtract(
    Isolate* isolate, Handle<JSTemporalDuration> duration,
    Handle<Object> other_obj, Handle<Object> options_obj, int factor,
    const char* method) {
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Set other to ? ToLimitedTemporalDuration(other, « »).
  Maybe<temporal::DurationRecord> maybe_other =
      temporal::ToLimitedTemporalDuration(isolate, other_obj,
                                          std::set<temporal::Unit>({}), method);
  MAYBE_RETURN(maybe_other, Handle<JSTemporalDuration>());
  temporal::DurationRecord other = maybe_other.FromJust();
  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalDuration);

  // 5. Let relativeTo be ? ToRelativeTemporalObject(options).
  Handle<Object> relative_to;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, relative_to,
      temporal::ToRelativeTemporalObject(isolate, options, method),
      JSTemporalDuration);
  // 6. Let result be ? AddDuration(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]], −other.[[Years]],
  // −other.[[Months]], −other.[[Weeks]], −other.[[Days]], −other.[[Hours]],
  // −other.[[Minutes]], −other.[[Seconds]], −other.[[Milliseconds]],
  // −other.[[Microseconds]], −other.[[Nanoseconds]], relativeTo).
  Maybe<temporal::DurationRecord> maybe_result = temporal::AddDuration(
      isolate,
      {duration->years().Number(), duration->months().Number(),
       duration->weeks().Number(), duration->days().Number(),
       duration->hours().Number(), duration->minutes().Number(),
       duration->seconds().Number(), duration->milliseconds().Number(),
       duration->microseconds().Number(), duration->nanoseconds().Number()},
      {factor * other.years, factor * other.months, factor * other.weeks,
       factor * other.days, factor * other.hours, factor * other.minutes,
       factor * other.seconds, factor * other.milliseconds,
       factor * other.microseconds, factor * other.nanoseconds},
      relative_to);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalDuration>());
  temporal::DurationRecord result = maybe_result.FromJust();

  // 7. Return ? CreateTemporalDuration(result.[[Years]], result.[[Months]],
  // result.[[Weeks]], result.[[Days]], result.[[Hours]], result.[[Minutes]],
  // result.[[Seconds]], result.[[Milliseconds]], result.[[Microseconds]],
  // result.[[Nanoseconds]]).
  return temporal::CreateTemporalDuration(
      isolate, result.years, result.months, result.weeks, result.days,
      result.hours, result.minutes, result.seconds, result.milliseconds,
      result.microseconds, result.nanoseconds);
}
}  // namespace

// #sec-temporal.duration.prototype.add
MaybeHandle<JSTemporalDuration> JSTemporalDuration::Add(
    Isolate* isolate, Handle<JSTemporalDuration> duration, Handle<Object> other,
    Handle<Object> options) {
  return AddOrSubtract(isolate, duration, other, options, 1,
                       "Temporal.Duration.prototype.add");
}

// #sec-temporal.duration.prototype.subtract
MaybeHandle<JSTemporalDuration> JSTemporalDuration::Subtract(
    Isolate* isolate, Handle<JSTemporalDuration> duration, Handle<Object> other,
    Handle<Object> options) {
  return AddOrSubtract(isolate, duration, other, options, -1,
                       "Temporal.Duration.prototype.subtract");
}

// #sec-temporal.duration.prototype.round
MaybeHandle<JSTemporalDuration> JSTemporalDuration::Round(
    Isolate* isolate, Handle<JSTemporalDuration> duration,
    Handle<Object> options_obj) {
  const char* method = "Temporal.Duration.prototype.round";
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. If options is undefined, then
  if (options_obj->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalDuration);
  }
  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalDuration);
  // 5. Let smallestUnitPresent be true.
  bool smallest_unit_present = true;
  // 6. Let largestUnitPresent be true.
  bool largest_unit_present = true;
  // 7. Let smallestUnit be ? ToSmallestTemporalUnit(options, « », undefined).
  Maybe<temporal::Unit> maybe_smallest_unit =
      ToSmallestTemporalUnit(isolate, options, std::set<temporal::Unit>({}),
                             temporal::Unit::kNotPresent, method);
  MAYBE_RETURN(maybe_smallest_unit, Handle<JSTemporalDuration>());
  temporal::Unit smallest_unit = maybe_smallest_unit.FromJust();
  // 8. If smallestUnit is undefined, then
  if (smallest_unit == temporal::Unit::kNotPresent) {
    // a. Set smallestUnitPresent to false.
    smallest_unit_present = false;
    // b. Set smallestUnit to "nanosecond".
    smallest_unit = temporal::Unit::kNanosecond;
  }
  // 9. Let defaultLargestUnit be !
  // DefaultTemporalLargestUnit(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]]).
  temporal::Unit default_largest_unit = temporal::DefaultTemporalLargestUnit(
      isolate,
      {duration->years().Number(), duration->months().Number(),
       duration->weeks().Number(), duration->days().Number(),
       duration->hours().Number(), duration->minutes().Number(),
       duration->seconds().Number(), duration->milliseconds().Number(),
       duration->microseconds().Number(), duration->nanoseconds().Number()});

  // 10. Set defaultLargestUnit to !
  // LargerOfTwoTemporalUnits(defaultLargestUnit, smallestUnit).
  default_largest_unit = temporal::LargerOfTwoTemporalUnits(
      isolate, default_largest_unit, smallest_unit);

  // 11. Let largestUnit be ? ToLargestTemporalUnit(options, « », undefined).
  Maybe<temporal::Unit> maybe_largest_unit = temporal::ToLargestTemporalUnit(
      isolate, options, std::set<temporal::Unit>({}),
      temporal::Unit::kNotPresent, temporal::Unit::kNotPresent, method);
  MAYBE_RETURN(maybe_largest_unit, Handle<JSTemporalDuration>());
  temporal::Unit largest_unit = maybe_largest_unit.FromJust();

  // 12. If largestUnit is undefined, then
  if (largest_unit == temporal::Unit::kNotPresent) {
    // a. Set largestUnitPresent to false.
    largest_unit_present = false;
    // b. Set largestUnit to defaultLargestUnit.
    largest_unit = default_largest_unit;
    // 13. Else if largestUnit is "auto", then
  } else if (largest_unit == temporal::Unit::kAuto) {
    // a. Set largestUnit to defaultLargestUnit.
    largest_unit = default_largest_unit;
  }
  // 14. If smallestUnitPresent is false and largestUnitPresent is false, then
  if (smallest_unit_present == false && largest_unit_present == false) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalDuration);
  }
  // 15. Perform ? ValidateTemporalUnitRange(largestUnit, smallestUnit).
  Maybe<bool> maybe_valid = temporal::ValidateTemporalUnitRange(
      isolate, largest_unit, smallest_unit, method);
  MAYBE_RETURN(maybe_valid, Handle<JSTemporalDuration>());
  CHECK(maybe_valid.FromJust());
  // 16. Let roundingMode be ? ToTemporalRoundingMode(options, "halfExpand").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(
          isolate, options, temporal::RoundingMode::kHalfExpand, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<JSTemporalDuration>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();
  // 17. Let maximum be !
  // MaximumTemporalDurationRoundingIncrement(smallestUnit).
  double maximum;
  Maybe<bool> maybe_maximum = MaximumTemporalDurationRoundingIncrement(
      isolate, smallest_unit, &maximum);
  MAYBE_RETURN(maybe_maximum, Handle<JSTemporalDuration>());
  bool maximum_is_defined = maybe_maximum.FromJust();
  // 18. Let roundingIncrement be ? ToTemporalRoundingIncrement(options,
  // maximum, false).
  Maybe<int> maybe_rounding_increment = temporal::ToTemporalRoundingIncrement(
      isolate, options, maximum, maximum_is_defined, false, method);
  MAYBE_RETURN(maybe_rounding_increment, Handle<JSTemporalDuration>());
  int rounding_increment = maybe_rounding_increment.FromJust();

  // 19. Let relativeTo be ? ToRelativeTemporalObject(options).
  Handle<Object> relative_to_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, relative_to_obj,
      temporal::ToRelativeTemporalObject(isolate, options, method),
      JSTemporalDuration);
  // 20. Let unbalanceResult be ? UnbalanceDurationRelative(duration.[[Years]],
  // duration.[[Months]], duration.[[Weeks]], duration.[[Days]], largestUnit,
  // relativeTo).
  double years = duration->years().Number();
  double months = duration->months().Number();
  double weeks = duration->weeks().Number();
  double days = duration->days().Number();
  Maybe<bool> maybe_unbalance_result = temporal::UnbalanceDurationRelative(
      isolate, &years, &months, &weeks, &days, largest_unit, relative_to_obj,
      method);
  MAYBE_RETURN(maybe_unbalance_result, Handle<JSTemporalDuration>());
  CHECK(maybe_unbalance_result.FromJust());
  // 21. Let roundResult be ? RoundDuration(unbalanceResult.[[Years]],
  // unbalanceResult.[[Months]], unbalanceResult.[[Weeks]],
  // unbalanceResult.[[Days]], duration.[[Hours]], duration.[[Minutes]],
  // duration[[Seconds]], duration[[Milliseconds]], duration.[[Microseconds]],
  // duration.[[Nanoseconds]], roundingIncrement, smallestUnit, roundingMode,
  // relativeTo).
  double remainder;
  Maybe<temporal::DurationRecord> maybe_round_result = RoundDuration(
      isolate,
      {years, months, weeks, days, duration->hours().Number(),
       duration->minutes().Number(), duration->seconds().Number(),
       duration->milliseconds().Number(), duration->microseconds().Number(),
       duration->nanoseconds().Number()},
      rounding_increment, smallest_unit, rounding_mode, &remainder, method);
  MAYBE_RETURN(maybe_round_result, Handle<JSTemporalDuration>());
  temporal::DurationRecord round_result = maybe_round_result.FromJust();

  // 22. Let adjustResult be ? AdjustRoundedDurationDays(roundResult.[[Years]],
  // roundResult.[[Months]], roundResult.[[Weeks]], roundResult.[[Days]],
  // roundResult.[[Hours]], roundResult.[[Minutes]], roundResult.[[Seconds]],
  // roundResult.[[Milliseconds]], roundResult.[[Microseconds]],
  // roundResult.[[Nanoseconds]], roundingIncrement, smallestUnit, roundingMode,
  // relativeTo).
  Maybe<temporal::DurationRecord> maybe_adjust_result =
      temporal::AdjustRoundedDurationDays(isolate, round_result,
                                          rounding_increment, smallest_unit,
                                          rounding_mode, relative_to_obj);
  MAYBE_RETURN(maybe_adjust_result, Handle<JSTemporalDuration>());
  temporal::DurationRecord adjust_result = maybe_adjust_result.FromJust();
  // 23. Let balanceResult be ? BalanceDurationRelative(adjustResult.[[Years]],
  // adjustResult.[[Months]], adjustResult.[[Weeks]], adjustResult.[[Days]],
  // largestUnit, relativeTo).
  Maybe<bool> maybe_balance_result = BalanceDurationRelative(
      isolate, &adjust_result.years, &adjust_result.months,
      &adjust_result.weeks, &adjust_result.days, largest_unit, relative_to_obj,
      method);
  MAYBE_RETURN(maybe_balance_result, Handle<JSTemporalDuration>());
  CHECK(maybe_balance_result.FromJust());

  // 24. If relativeTo has an [[InitializedTemporalZonedDateTime]] internal
  // slot, then
  if (relative_to_obj->IsJSTemporalZonedDateTime()) {
    // a. Set relativeTo to ? MoveRelativeZonedDateTime(relativeTo,
    // balanceResult.[[Years]], balanceResult.[[Months]],
    // balanceResult.[[Weeks]], 0).

    Handle<JSTemporalZonedDateTime> relative_to =
        Handle<JSTemporalZonedDateTime>::cast(relative_to_obj);
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, relative_to_obj,
        MoveRelativeZonedDateTime(isolate, relative_to, adjust_result.years,
                                  adjust_result.months, adjust_result.weeks, 0),
        JSTemporalDuration);
  }
  // 25. Let result be ? BalanceDuration(balanceResult.[[Days]],
  // adjustResult.[[Hours]], adjustResult.[[Minutes]], adjustResult.[[Seconds]],
  // adjustResult.[[Milliseconds]], adjustResult.[[Microseconds]],
  // adjustResult.[[Nanoseconds]], largestUnit, relativeTo).
  Maybe<bool> maybe_result = BalanceDuration(
      isolate, &adjust_result.days, &adjust_result.hours,
      &adjust_result.minutes, &adjust_result.seconds,
      &adjust_result.milliseconds, &adjust_result.microseconds,
      &adjust_result.nanoseconds, largest_unit, relative_to_obj);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalDuration>());
  CHECK(maybe_result.FromJust());

  // 26. Return ? CreateTemporalDuration(balanceResult.[[Years]],
  // balanceResult.[[Months]], balanceResult.[[Weeks]], result.[[Days]],
  // result.[[Hours]], result.[[Minutes]], result.[[Seconds]],
  // result.[[Milliseconds]], result.[[Microseconds]], result.[[Nanoseconds]]).
  return temporal::CreateTemporalDuration(
      isolate, adjust_result.years, adjust_result.months, adjust_result.weeks,
      adjust_result.days, adjust_result.hours, adjust_result.minutes,
      adjust_result.seconds, adjust_result.milliseconds,
      adjust_result.microseconds, adjust_result.nanoseconds);
}

// #sec-temporal.duration.prototype.total
MaybeHandle<Object> JSTemporalDuration::Total(
    Isolate* isolate, Handle<JSTemporalDuration> duration,
    Handle<Object> options_obj) {
  const char* method = "Temporal.Duration.prototype.total";
  Factory* factory = isolate->factory();
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method), String);
  // 4. Let relativeTo be ? ToRelativeTemporalObject(options).
  Handle<Object> relative_to_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, relative_to_obj,
      temporal::ToRelativeTemporalObject(isolate, options, method), Object);
  // 5. Let unit be ? ToTemporalDurationTotalUnit(options).
  Maybe<temporal::Unit> maybe_unit =
      temporal::ToTemporalDurationTotalUnit(isolate, options, method);
  MAYBE_RETURN(maybe_unit, Handle<Object>());
  temporal::Unit unit = maybe_unit.FromJust();

  // 6. Let unbalanceResult be ? UnbalanceDurationRelative(duration.[[Years]],
  // duration.[[Months]], duration.[[Weeks]], duration.[[Days]], unit,
  // relativeTo).
  temporal::DurationRecord dur;
  dur.years = duration->years().Number();
  dur.months = duration->months().Number();
  dur.weeks = duration->weeks().Number();
  dur.days = duration->days().Number();
  Maybe<bool> maybe_unbalance_result = temporal::UnbalanceDurationRelative(
      isolate, &dur.years, &dur.months, &dur.weeks, &dur.days, unit,
      relative_to_obj, method);
  MAYBE_RETURN(maybe_unbalance_result, Handle<Object>());
  CHECK(maybe_unbalance_result.FromJust());

  // 7. Let intermediate be undefined.
  Handle<Object> intermediate = factory->undefined_value();
  // 8. If relativeTo has an [[InitializedTemporalZonedDateTime]] internal slot,
  // then
  if (relative_to_obj->IsJSTemporalZonedDateTime()) {
    Handle<JSTemporalZonedDateTime> relative_to =
        Handle<JSTemporalZonedDateTime>::cast(relative_to_obj);
    // a. Set intermediate to ? MoveRelativeZonedDateTime(relativeTo,
    // unbalanceResult.[[Years]], unbalanceResult.[[Months]],
    // unbalanceResult.[[Weeks]], 0).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, intermediate,
        MoveRelativeZonedDateTime(isolate, relative_to, dur.years, dur.months,
                                  dur.weeks, 0),
        Object);
  }
  // 9. Let balanceResult be ? BalanceDuration(unbalanceResult.[[Days]],
  // duration.[[Hours]], duration.[[Minutes]],
  // duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]], unit,
  // intermediate).
  dur.hours = duration->hours().Number();
  dur.minutes = duration->minutes().Number();
  dur.seconds = duration->seconds().Number();
  dur.milliseconds = duration->milliseconds().Number();
  dur.microseconds = duration->microseconds().Number();
  dur.nanoseconds = duration->nanoseconds().Number();
  Maybe<bool> maybe_balance_result =
      BalanceDuration(isolate, &dur.days, &dur.hours, &dur.minutes,
                      &dur.seconds, &dur.milliseconds, &dur.microseconds,
                      &dur.nanoseconds, unit, intermediate);
  MAYBE_RETURN(maybe_balance_result, Handle<Object>());
  CHECK(maybe_balance_result.FromJust());
  // 10. Let roundResult be ? RoundDuration(unbalanceResult.[[Years]],
  // unbalanceResult.[[Months]], unbalanceResult.[[Weeks]],
  // balanceResult.[[Days]], balanceResult.[[Hours]], balanceResult.[[Minutes]],
  // balanceResult.[[Seconds]], balanceResult.[[Milliseconds]],
  // balanceResult.[[Microseconds]], balanceResult.[[Nanoseconds]], 1, unit,
  // "trunc", relativeTo).
  double remainder;
  Maybe<temporal::DurationRecord> maybe_round_result = temporal::RoundDuration(
      isolate, dur, 1, unit, temporal::RoundingMode::kTrunc, relative_to_obj,
      &remainder, method);

  MAYBE_RETURN(maybe_round_result, Handle<String>());
  temporal::DurationRecord round_result = maybe_round_result.FromJust();
  double whole;
  switch (unit) {
    // 11. If unit is "year", then
    case temporal::Unit::kYear:
      // a. Let whole be roundResult.[[Years]].
      whole = round_result.years;
      break;
    // 12. If unit is "month", then
    case temporal::Unit::kMonth:
      // a. Let whole be roundResult.[[Months]].
      whole = round_result.months;
      break;
    // 13. If unit is "week", then
    case temporal::Unit::kWeek:
      // a. Let whole be roundResult.[[Weeks]].
      whole = round_result.weeks;
      break;
    // 14. If unit is "day", then
    case temporal::Unit::kDay:
      // a. Let whole be roundResult.[[Days]].
      whole = round_result.days;
      break;
    // 15. If unit is "hour", then
    case temporal::Unit::kHour:
      // a. Let whole be roundResult.[[Hours]].
      whole = round_result.hours;
      break;
    // 16. If unit is "minute", then
    case temporal::Unit::kMinute:
      // a. Let whole be roundResult.[[Minutes]].
      whole = round_result.minutes;
      break;
    // 17. If unit is "second", then
    case temporal::Unit::kSecond:
      // a. Let whole be roundResult.[[Seconds]].
      whole = round_result.seconds;
      break;
    // 18. If unit is "millisecond", then
    case temporal::Unit::kMillisecond:
      // a. Let whole be roundResult.[[Milliseconds]].
      whole = round_result.milliseconds;
      break;
    // 19. If unit is "microsecond", then
    case temporal::Unit::kMicrosecond:
      // a. Let whole be roundResult.[[Microseconds]].
      whole = round_result.microseconds;
      break;
    // 20. If unit is "naoosecond", then
    case temporal::Unit::kNanosecond:
      // a. Let whole be roundResult.[[Nanoseconds]].
      whole = round_result.nanoseconds;
      break;
    default:
      UNREACHABLE();
  }
  // 21. Return whole + roundResult.[[Remainder]].
  return factory->NewNumber(whole + remainder);
}

// #sec-temporal.duration.prototype.tostring
MaybeHandle<String> JSTemporalDuration::ToString(
    Isolate* isolate, Handle<JSTemporalDuration> duration,
    Handle<Object> options_obj) {
  const char* method = "Temporal.Duration.prototype.toString";
  // 3. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method), String);
  // 4. Let precision be ? ToSecondsStringPrecision(options).
  temporal::Precision precision;
  double increment;
  temporal::Unit unit;
  Maybe<bool> maybe_precision = temporal::ToSecondsStringPrecision(
      isolate, options, &precision, &increment, &unit, method);
  MAYBE_RETURN(maybe_precision, Handle<String>());
  CHECK(maybe_precision.FromJust());

  // 5. If precision.[[Unit]] is "minute", throw a RangeError exception.
  if (unit == temporal::Unit::kMinute) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), String);
  }
  // 6. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(isolate, options,
                                       temporal::RoundingMode::kTrunc, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<String>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();

  // 7. Let result be ? RoundDuration(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]],
  // precision.[[Increment]], precision.[[Unit]], roundingMode).
  double remainder;
  Maybe<temporal::DurationRecord> maybe_result = temporal::RoundDuration(
      isolate,
      {duration->years().Number(), duration->months().Number(),
       duration->weeks().Number(), duration->days().Number(),
       duration->hours().Number(), duration->minutes().Number(),
       duration->seconds().Number(), duration->milliseconds().Number(),
       duration->microseconds().Number(), duration->nanoseconds().Number()},
      increment, unit, rounding_mode, &remainder, method);
  MAYBE_RETURN(maybe_result, Handle<String>());
  temporal::DurationRecord result = maybe_result.FromJust();
  // 8. Return ! TemporalDurationToString(result.[[Years]], result.[[Months]],
  // result.[[Weeks]], result.[[Days]], result.[[Hours]], result.[[Minutes]],
  // result.[[Seconds]], result.[[Milliseconds]], result.[[Microseconds]],
  // result.[[Nanoseconds]], precision.[[Precision]]).

  return temporal::TemporalDurationToString(isolate, result, precision);
}

// #sec-temporal.duration.prototype.tolocalestring
MaybeHandle<String> JSTemporalDuration::ToLocaleString(
    Isolate* isolate, Handle<JSTemporalDuration> duration,
    Handle<Object> locales, Handle<Object> options) {
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Return ! TemporalDurationToString(duration.[[Years]],
  // duration.[[Months]], duration.[[Weeks]], duration.[[Days]],
  // duration.[[Hours]], duration.[[Minutes]], duration.[[Seconds]],
  // duration.[[Milliseconds]], duration.[[Microseconds]],
  // duration.[[Nanoseconds]], "auto").
  return temporal::TemporalDurationToString(
      isolate,
      {duration->years().Number(), duration->months().Number(),
       duration->weeks().Number(), duration->days().Number(),
       duration->hours().Number(), duration->minutes().Number(),
       duration->seconds().Number(), duration->milliseconds().Number(),
       duration->microseconds().Number(), duration->nanoseconds().Number()},
      temporal::Precision::kAuto);
}

// #sec-temporal.duration.prototype.tojson
MaybeHandle<String> JSTemporalDuration::ToJSON(
    Isolate* isolate, Handle<JSTemporalDuration> duration) {
  // 1. Let duration be the this value.
  // 2. Perform ? RequireInternalSlot(duration,
  // [[InitializedTemporalDuration]]).
  // 3. Return ! TemporalDurationToString(duration.[[Years]],
  // duration.[[Months]], duration.[[Weeks]], duration.[[Days]],
  // duration.[[Hours]], duration.[[Minutes]], duration.[[Seconds]],
  // duration.[[Milliseconds]], duration.[[Microseconds]],
  // duration.[[Nanoseconds]], "auto").

  return temporal::TemporalDurationToString(
      isolate,
      {duration->years().Number(), duration->months().Number(),
       duration->weeks().Number(), duration->days().Number(),
       duration->hours().Number(), duration->minutes().Number(),
       duration->seconds().Number(), duration->milliseconds().Number(),
       duration->microseconds().Number(), duration->nanoseconds().Number()},
      temporal::Precision::kAuto);
}

namespace {

// #sec-temporal-regulateisoyearmonth
Maybe<bool> RegulateISOYearMonth(Isolate* isolate, int32_t* year,
                                 int32_t* month,
                                 temporal::ShowOverflow overflow) {
  // 1. Assert: year and month are integers.
  // 2. Assert: overflow is either "constrain" or "reject".
  switch (overflow) {
    // 3. If overflow is "constrain", then
    case temporal::ShowOverflow::kConstrain:
      // a. Return ! ConstrainISOYearMonth(year, month).
      *month = std::max(std::min(*month, 12), 1);
      return Just(true);
    // 4. If overflow is "reject", then
    case temporal::ShowOverflow::kReject:
      // a. If ! IsValidISOMonth(month) is false, throw a RangeError exception.
      if (!temporal::IsValidISOMonth(isolate, *month)) {
        THROW_NEW_ERROR_RETURN_VALUE(
            isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(), Nothing<bool>());
      }
      // b. Return the new Record { [[Year]]: year, [[Month]]: month }.
      return Just(true);
    default:
      UNREACHABLE();
  }
}

// #sec-temporal-resolveisomonth
Maybe<int32_t> ResolveISOMonth(Isolate* isolate, Handle<JSReceiver> fields) {
  Factory* factory = isolate->factory();
  // 1. Let month be ? Get(fields, "month").
  Handle<Object> month_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, month_obj,
      Object::GetPropertyOrElement(isolate, fields, factory->month_string()),
      Nothing<int32_t>());
  // 2. Let monthCode be ? Get(fields, "monthCode").
  Handle<Object> month_code_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, month_code_obj,
      Object::GetPropertyOrElement(isolate, fields,
                                   factory->monthCode_string()),
      Nothing<int32_t>());
  // 3. If monthCode is undefined, then
  if (month_code_obj->IsUndefined(isolate)) {
    // a. If month is undefined, throw a TypeError exception.
    if (month_obj->IsUndefined(isolate)) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(), Nothing<int32_t>());
    }
    // b. Return month.
    return Just(FastD2I(floor(month_obj->Number())));
  }
  // 4. Assert: Type(monthCode) is String.
  CHECK(month_code_obj->IsString());
  Handle<String> month_code;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, month_code,
                                   Object::ToString(isolate, month_code_obj),
                                   Nothing<int32_t>());
  std::unique_ptr<char[]> month_code_cstr = month_code->ToCString();
  // 5. Let monthLength be the length of monthCode.
  // 6. If monthLength is not 3, throw a RangeError exception.
  if (std::strlen(month_code_cstr.get()) != 3) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                      factory->monthCode_string()),
        Nothing<int32_t>());
  }
  // 7. Let numberPart be the substring of monthCode from 1.
  // 8. Set numberPart to ! ToIntegerOrInfinity(numberPart).
  int32_t number_part;
  if (!month_obj->IsUndefined()) {
    number_part = FastD2I(floor(month_obj->Number()));
    std::ostringstream month_stream;
    month_stream << "M" << std::setfill('0') << std::setw(2) << number_part;
    // 10. If month is not undefined, and month ≠ numberPart, then
    if (month_stream.str() != month_code_cstr.get() || number_part < 1 ||
        number_part > 12) {
      // a. Throw a RangeError exception.
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                        factory->monthCode_string()),
          Nothing<int32_t>());
    }
  } else {
    // 11. If ! SameValueNonNumeric(monthCode, ! BuildISOMonthCode(numberPart))
    // is false, then
    if ((month_code_cstr[0] == 'M') &&
        // 9. If numberPart < 1 or numberPart > 12, throw a RangeError
        // exception.
        ((month_code_cstr[1] == '0' && '1' <= month_code_cstr[2] &&
          month_code_cstr[2] <= '9') ||
         (month_code_cstr[1] == '1' && '0' <= month_code_cstr[2] &&
          month_code_cstr[2] <= '2'))) {
      number_part = 10 * static_cast<int32_t>(month_code_cstr[1] - '0') +
                    static_cast<int32_t>(month_code_cstr[2] - '0');
      CHECK_GE(number_part, 1);
      CHECK_LE(number_part, 12);
    } else {
      // a. Throw a RangeError exception.
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                        factory->monthCode_string()),
          Nothing<int32_t>());
    }
  }
  // 12. Return numberPart.
  return Just(number_part);
}

Maybe<bool> ISODateFromFields(Isolate* isolate, Handle<JSReceiver> fields,
                              Handle<JSReceiver> options, int32_t* year,
                              int32_t* month, int32_t* day,
                              const char* method) {
  Factory* factory = isolate->factory();

  // 1. Assert: Type(fields) is Object.
  // 2. Let overflow be ? ToTemporalOverflow(options).
  Maybe<temporal::ShowOverflow> maybe_overflow =
      temporal::ToTemporalOverflow(isolate, options, method);
  MAYBE_RETURN(maybe_overflow, Nothing<bool>());
  // 3. Set fields to ? PrepareTemporalFields(fields, « "day", "month",
  // "monthCode", "year" », «»).
  Handle<FixedArray> field_names = factory->NewFixedArray(4);
  field_names->set(0, *(factory->day_string()));
  field_names->set(1, *(factory->month_string()));
  field_names->set(2, *(factory->monthCode_string()));
  field_names->set(3, *(factory->year_string()));
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, fields,
      temporal::PrepareTemporalFields(isolate, fields, field_names, false,
                                      false),
      Nothing<bool>());

  // 4. Let year be ? Get(fields, "year").
  Handle<Object> year_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, year_obj,
      Object::GetPropertyOrElement(isolate, fields, factory->year_string()),
      Nothing<bool>());
  // 5. If year is undefined, throw a TypeError exception.
  if (year_obj->IsUndefined(isolate)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                                 Nothing<bool>());
  }
  *year = FastD2I(floor(year_obj->Number()));

  // 6. Let month be ? ResolveISOMonth(fields).
  Maybe<int32_t> maybe_month = ResolveISOMonth(isolate, fields);
  MAYBE_RETURN(maybe_month, Nothing<bool>());
  *month = maybe_month.FromJust();

  // 7. Let day be ? Get(fields, "day").
  Handle<Object> day_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, day_obj,
      Object::GetPropertyOrElement(isolate, fields, factory->day_string()),
      Nothing<bool>());
  // 8. If day is undefined, throw a TypeError exception.
  if (day_obj->IsUndefined(isolate)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                                 Nothing<bool>());
  }
  *day = FastD2I(floor(day_obj->Number()));
  // 9. Return ? RegulateISODate(year, month, day, overflow).
  return RegulateISODate(isolate, year, month, day, maybe_overflow.FromJust());
}

Maybe<bool> ISOYearMonthFromFields(Isolate* isolate, Handle<JSReceiver> fields,
                                   Handle<JSReceiver> options, int32_t* year,
                                   int32_t* month, int32_t* reference_iso_day,
                                   const char* method) {
  Factory* factory = isolate->factory();
  // 1. Assert: Type(fields) is Object.
  // 2. Let overflow be ? ToTemporalOverflow(options).
  Maybe<temporal::ShowOverflow> maybe_overflow =
      temporal::ToTemporalOverflow(isolate, options, method);
  MAYBE_RETURN(maybe_overflow, Nothing<bool>());
  // 3. Set fields to ? PrepareTemporalFields(fields, « "month", "monthCode",
  // "year" », «»).
  Handle<FixedArray> field_names = factory->NewFixedArray(3);
  field_names->set(0, *(factory->month_string()));
  field_names->set(1, *(factory->monthCode_string()));
  field_names->set(2, *(factory->year_string()));
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, fields,
      temporal::PrepareTemporalFields(isolate, fields, field_names, false,
                                      false),
      Nothing<bool>());

  // 4. Let year be ? Get(fields, "year").
  Handle<Object> year_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, year_obj,
      Object::GetPropertyOrElement(isolate, fields, factory->year_string()),
      Nothing<bool>());
  // 5. If year is undefined, throw a TypeError exception.
  if (year_obj->IsUndefined(isolate)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                                 Nothing<bool>());
  }
  *year = FastD2I(floor(year_obj->Number()));
  // 6. Let month be ? ResolveISOMonth(fields).
  Maybe<int32_t> maybe_month = ResolveISOMonth(isolate, fields);
  MAYBE_RETURN(maybe_month, Nothing<bool>());
  *month = maybe_month.FromJust();
  // 7. Let result be ? RegulateISOYearMonth(year, month, overflow).
  // 8. Return the new Record { [[Year]]: result.[[Year]], [[Month]]:
  // result.[[Month]], [[ReferenceISODay]]: 1 }.
  *reference_iso_day = 1;
  return RegulateISOYearMonth(isolate, year, month, maybe_overflow.FromJust());
}

Maybe<bool> ISOMonthDayFromFields(Isolate* isolate, Handle<JSReceiver> fields,
                                  Handle<JSReceiver> options, int32_t* month,
                                  int32_t* day, int32_t* reference_iso_year,
                                  const char* method) {
  Factory* factory = isolate->factory();
  // 1. Assert: Type(fields) is Object.
  // 2. Let overflow be ? ToTemporalOverflow(options).
  Maybe<temporal::ShowOverflow> maybe_overflow =
      temporal::ToTemporalOverflow(isolate, options, method);
  MAYBE_RETURN(maybe_overflow, Nothing<bool>());
  // 3. Set fields to ? PrepareTemporalFields(fields, « "day", "month",
  // "monthCode", "year" », «»).
  Handle<FixedArray> field_names = factory->NewFixedArray(4);
  field_names->set(0, *(factory->day_string()));
  field_names->set(1, *(factory->month_string()));
  field_names->set(2, *(factory->monthCode_string()));
  field_names->set(3, *(factory->year_string()));
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, fields,
      temporal::PrepareTemporalFields(isolate, fields, field_names, false,
                                      false),
      Nothing<bool>());
  // 4. Let month be ? Get(fields, "month").
  Handle<Object> month_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, month_obj,
      Object::GetPropertyOrElement(isolate, fields, factory->month_string()),
      Nothing<bool>());
  // 5. Let monthCode be ? Get(fields, "monthCode").
  Handle<Object> month_code_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, month_code_obj,
      Object::GetPropertyOrElement(isolate, fields,
                                   factory->monthCode_string()),
      Nothing<bool>());
  // 6. Let year be ? Get(fields, "year").
  Handle<Object> year_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, year_obj,
      Object::GetPropertyOrElement(isolate, fields, factory->year_string()),
      Nothing<bool>());
  // 7. If month is not undefined, and monthCode and year are both undefined,
  // then
  if ((!month_obj->IsUndefined(isolate)) &&
      (month_code_obj->IsUndefined(isolate) &&
       year_obj->IsUndefined(isolate))) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                                 Nothing<bool>());
  }
  // 8. Set month to ? ResolveISOMonth(fields).
  Maybe<int32_t> maybe_month = ResolveISOMonth(isolate, fields);
  MAYBE_RETURN(maybe_month, Nothing<bool>());
  *month = maybe_month.FromJust();
  // 9. Let day be ? Get(fields, "day").
  Handle<Object> day_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, day_obj,
      Object::GetPropertyOrElement(isolate, fields, factory->day_string()),
      Nothing<bool>());
  // 10. If day is undefined, throw a TypeError exception.
  if (day_obj->IsUndefined(isolate)) {
    THROW_NEW_ERROR_RETURN_VALUE(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                                 Nothing<bool>());
  }
  *day = FastD2I(floor(day_obj->Number()));
  // 11. Let referenceISOYear be 1972 (the first leap year after the Unix
  // epoch).
  *reference_iso_year = 1972;
  // 12. If monthCode is undefined, then
  if (month_code_obj->IsUndefined(isolate)) {
    int32_t year = FastD2I(floor(year_obj->Number()));
    // a. Let result be ? RegulateISODate(year, month, day, overflow).
    return RegulateISODate(isolate, &year, month, day,
                           maybe_overflow.FromJust());
  } else {
    // 13. Else,
    // a. Let result be ? RegulateISODate(referenceISOYear, month, day,
    // overflow).
    return RegulateISODate(isolate, reference_iso_year, month, day,
                           maybe_overflow.FromJust());
  }
  // 14. Return the new Record { [[Month]]: result.[[Month]], [[Day]]:
  // result.[[Day]], [[ReferenceISOYear]]: referenceISOYear }.
}

}  // namespace

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

// #sec-temporal.calendar.from
MaybeHandle<JSReceiver> JSTemporalCalendar::From(Isolate* isolate,
                                                 Handle<Object> item) {
  // 1. Return ? ToTemporalCalendar(item).
  return temporal::ToTemporalCalendar(isolate, item, "Temporal.Calendar.from");
}

// #sec-temporal.calendar.prototype.datefromfields
MaybeHandle<JSTemporalPlainDate> JSTemporalCalendar::DateFromFields(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> fields_obj, Handle<Object> options_obj) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(fields) is not Object, throw a TypeError exception.
  const char* method = "Temporal.Calendar.prototype.dateFromFields";
  if (!fields_obj->IsJSReceiver()) {
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kCalledOnNonObject,
                     isolate->factory()->NewStringFromAsciiChecked(method)),
        JSTemporalPlainDate);
  }
  Handle<JSReceiver> fields = Handle<JSReceiver>::cast(fields_obj);

  // 5. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainDate);
  // 6. Let result be ? ISODateFromFields(fields, options).
  int32_t year;
  int32_t month;
  int32_t day;
  Maybe<bool> maybe_result =
      ISODateFromFields(isolate, fields, options, &year, &month, &day, method);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalPlainDate>());
  // 7. Return ? CreateTemporalDate(result.[[Year]], result.[[Month]],
  // result.[[Day]], calendar).
  return temporal::CreateTemporalDate(isolate, year, month, day, calendar);
}

// #sec-temporal.calendar.prototype.yearmonthfromfields
MaybeHandle<JSTemporalPlainYearMonth> JSTemporalCalendar::YearMonthFromFields(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> fields_obj, Handle<Object> options_obj) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  const char* method = "Temporal.Calendar.prototype.yearMonthFromFields";
  // 4. If Type(fields) is not Object, throw a TypeError exception.
  if (!fields_obj->IsJSReceiver()) {
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kCalledOnNonObject,
                     isolate->factory()->NewStringFromAsciiChecked(method)),
        JSTemporalPlainYearMonth);
  }
  Handle<JSReceiver> fields = Handle<JSReceiver>::cast(fields_obj);
  // 5. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainYearMonth);
  // 6. Let result be ? ISOYearMonthFromFields(fields, options).
  int32_t year;
  int32_t month;
  int32_t reference_iso_day;
  Maybe<bool> maybe_result = ISOYearMonthFromFields(
      isolate, fields, options, &year, &month, &reference_iso_day, method);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalPlainYearMonth>());
  // 7. Return ? CreateTemporalYearMonth(result.[[Year]], result.[[Month]],
  // calendar, result.[[ReferenceISODay]]).
  return temporal::CreateTemporalYearMonth(isolate, year, month, calendar,
                                           reference_iso_day);
}

// #sec-temporal.calendar.prototype.monthdayfromfields
MaybeHandle<JSTemporalPlainMonthDay> JSTemporalCalendar::MonthDayFromFields(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> fields_obj, Handle<Object> options_obj) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  const char* method = "Temporal.Calendar.prototype.monthDayFromFields";
  // 4. If Type(fields) is not Object, throw a TypeError exception.
  if (!fields_obj->IsJSReceiver()) {
    THROW_NEW_ERROR(
        isolate,
        NewTypeError(MessageTemplate::kCalledOnNonObject,
                     isolate->factory()->NewStringFromAsciiChecked(method)),
        JSTemporalPlainMonthDay);
  }
  Handle<JSReceiver> fields = Handle<JSReceiver>::cast(fields_obj);
  // 5. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainMonthDay);
  // 6. Let result be ? ISOMonthDayFromFields(fields, options).
  int32_t reference_iso_year;
  int32_t month;
  int32_t day;
  Maybe<bool> maybe_result = ISOMonthDayFromFields(
      isolate, fields, options, &month, &day, &reference_iso_year, method);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalPlainMonthDay>());
  // 7. Return ? CreateTemporalMonthDay(result.[[Month]], result.[[Day]],
  // calendar, result.[[ReferenceISOYear]]).
  return temporal::CreateTemporalMonthDay(isolate, month, day, calendar,
                                          reference_iso_year);
}

// #sec-temporal.calendar.prototype.dateadd
MaybeHandle<JSTemporalPlainDate> JSTemporalCalendar::DateAdd(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> date_obj, Handle<Object> duration_obj,
    Handle<Object> options_obj) {
  const char* method = "Temporal.Calendar.prototype.dateAdd";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. Set date to ? ToTemporalDate(date).
  Handle<JSTemporalPlainDate> date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date, temporal::ToTemporalDate(isolate, date_obj, method),
      JSTemporalPlainDate);
  // 5. Set duration to ? ToTemporalDuration(duration).
  Handle<JSTemporalDuration> duration;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, duration,
      temporal::ToTemporalDuration(isolate, duration_obj, method),
      JSTemporalPlainDate);
  // 6. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainDate);
  // 7. Let overflow be ? ToTemporalOverflow(options).
  Maybe<temporal::ShowOverflow> maybe_overflow =
      temporal::ToTemporalOverflow(isolate, options, method);
  MAYBE_RETURN(maybe_overflow, Handle<JSTemporalPlainDate>());

  // 8. Let balanceResult be ! BalanceDuration(duration.[[Days]],
  // duration.[[Hours]], duration.[[Minutes]], duration.[[Seconds]],
  // duration.[[Milliseconds]], duration.[[Microseconds]],
  // duration.[[Nanoseconds]], "day").
  double days = duration->days().Number();
  double hours = duration->hours().Number();
  double minutes = duration->minutes().Number();
  double seconds = duration->seconds().Number();
  double milliseconds = duration->milliseconds().Number();
  double microseconds = duration->microseconds().Number();
  double nanoseconds = duration->nanoseconds().Number();
  Maybe<bool> maybe_balance_result =
      BalanceDuration(isolate, &days, &hours, &minutes, &seconds, &milliseconds,
                      &microseconds, &nanoseconds, temporal::Unit::kDay);
  MAYBE_RETURN(maybe_balance_result, Handle<JSTemporalPlainDate>());
  CHECK(maybe_balance_result.FromJust());

  // 9. Let result be ? AddISODate(date.[[ISOYear]], date.[[ISOMonth]],
  // date.[[ISODay]], duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], balanceResult.[[Days]], overflow).
  int32_t year, month, day;
  Maybe<bool> maybe_result =
      AddISODate(isolate, date->iso_year(), date->iso_month(), date->iso_day(),
                 duration->years().Number(), duration->months().Number(),
                 duration->weeks().Number(), days, maybe_overflow.FromJust(),
                 &year, &month, &day);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalPlainDate>());

  // 10. Return ? CreateTemporalDate(result.[[Year]], result.[[Month]],
  // result.[[Day]], calendar).
  return temporal::CreateTemporalDate(isolate, year, month, day, calendar);
}

// #sec-temporal.calendar.prototype.dateuntil
MaybeHandle<JSTemporalDuration> JSTemporalCalendar::DateUntil(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> one_obj, Handle<Object> two_obj,
    Handle<Object> options_obj) {
  const char* method = "Temporal.Calendar.prototype.dateUntil";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. Set one to ? ToTemporalDate(one).
  Handle<JSTemporalPlainDate> one;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, one,
                             temporal::ToTemporalDate(isolate, one_obj, method),
                             JSTemporalDuration);
  // 5. Set two to ? ToTemporalDate(two).
  Handle<JSTemporalPlainDate> two;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, two,
                             temporal::ToTemporalDate(isolate, two_obj, method),
                             JSTemporalDuration);
  // 6. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalDuration);
  // 7. Let largestUnit be ? ToLargestTemporalUnit(options, « "hour", "minute",
  // "second", "millisecond", "microsecond", "nanosecond" », "auto", "day").
  Maybe<temporal::Unit> maybe_largest_unit = ToLargestTemporalUnit(
      isolate, options,
      std::set<temporal::Unit>(
          {temporal::Unit::kHour, temporal::Unit::kMinute,
           temporal::Unit::kSecond, temporal::Unit::kMillisecond,
           temporal::Unit::kMicrosecond, temporal::Unit::kNanosecond}),
      temporal::Unit::kAuto, temporal::Unit::kDay, method);
  MAYBE_RETURN(maybe_largest_unit, Handle<JSTemporalDuration>());
  // 8. Let result be ! DifferenceISODate(one.[[ISOYear]], one.[[ISOMonth]],
  // one.[[ISODay]], two.[[ISOYear]], two.[[ISOMonth]], two.[[ISODay]],
  // largestUnit).
  double years, months, weeks, days;
  Maybe<bool> maybe_result = DifferenceISODate(
      isolate, one->iso_year(), one->iso_month(), one->iso_day(),
      two->iso_year(), two->iso_month(), two->iso_day(),
      maybe_largest_unit.FromJust(), &years, &months, &weeks, &days, method);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalDuration>());
  // 9. Return ? CreateTemporalDuration(result.[[Years]], result.[[Months]],
  // result.[[Weeks]], result.[[Days]], 0, 0, 0, 0, 0, 0).
  return temporal::CreateTemporalDuration(isolate, years, months, weeks, days,
                                          0, 0, 0, 0, 0, 0);
}

#define GET_FROM_DATE_AND_YEAR_MONTH(name, RT)                                 \
  if (temporal_date_like->IsJSTemporalPlainDate()) {                           \
    name = Handle<JSTemporalPlainDate>::cast(temporal_date_like)->name();      \
  } else if (temporal_date_like->IsJSTemporalPlainDateTime()) {                \
    name = Handle<JSTemporalPlainDateTime>::cast(temporal_date_like)->name();  \
  } else if (temporal_date_like->IsJSTemporalPlainYearMonth()) {               \
    name = Handle<JSTemporalPlainYearMonth>::cast(temporal_date_like)->name(); \
  } else {                                                                     \
    /* a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike). */       \
    Handle<JSTemporalPlainDate> date;                                          \
    ASSIGN_RETURN_ON_EXCEPTION(                                                \
        isolate, date,                                                         \
        temporal::ToTemporalDate(isolate, temporal_date_like, method), RT);    \
    name = date->name();                                                       \
  }

#define GET_FROM_DATE_AND_MONTH_DAY(name, RT)                                 \
  if (temporal_date_like->IsJSTemporalPlainDate()) {                          \
    name = Handle<JSTemporalPlainDate>::cast(temporal_date_like)->name();     \
  } else if (temporal_date_like->IsJSTemporalPlainDateTime()) {               \
    name = Handle<JSTemporalPlainDateTime>::cast(temporal_date_like)->name(); \
  } else if (temporal_date_like->IsJSTemporalPlainMonthDay()) {               \
    name = Handle<JSTemporalPlainMonthDay>::cast(temporal_date_like)->name(); \
  } else {                                                                    \
    /* a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike). */      \
    Handle<JSTemporalPlainDate> date;                                         \
    ASSIGN_RETURN_ON_EXCEPTION(                                               \
        isolate, date,                                                        \
        temporal::ToTemporalDate(isolate, temporal_date_like, method), RT);   \
    name = date->name();                                                      \
  }

#define GET_FROM_DATE_AND_YEAR_MONTH_AND_MONTH_DAY(name, RT)                  \
  if (temporal_date_like->IsJSTemporalPlainMonthDay()) {                      \
    name = Handle<JSTemporalPlainMonthDay>::cast(temporal_date_like)->name(); \
  } else {                                                                    \
    GET_FROM_DATE_AND_YEAR_MONTH(name, RT)                                    \
  }

// #sec-temporal.calendar.prototype.year
MaybeHandle<Smi> JSTemporalCalendar::Year(Isolate* isolate,
                                          Handle<JSTemporalCalendar> calendar,
                                          Handle<Object> temporal_date_like) {
  const char* method = "Temporal.Calendar.prototype.year";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]],
  // or [[InitializedTemporalYearMonth]]
  // internal slot, then
  int32_t iso_year;
  GET_FROM_DATE_AND_YEAR_MONTH(iso_year, Smi)
  // 5. Return ! ISOYear(temporalDateLike).
  return Handle<Smi>(Smi::FromInt(iso_year), isolate);
}

// #sec-temporal.calendar.prototype.month
MaybeHandle<Smi> JSTemporalCalendar::Month(Isolate* isolate,
                                           Handle<JSTemporalCalendar> calendar,
                                           Handle<Object> temporal_date_like) {
  const char* method = "Temporal.Calendar.prototype.month";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(temporalDateLike) is Object and temporalDateLike has an
  // [[InitializedTemporalMonthDay]] internal slot, then
  if (temporal_date_like->IsJSTemporalPlainMonthDay()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(), Smi);
  }
  // 5. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]],
  // or [[InitializedTemporalYearMonth]]
  // internal slot, then
  int32_t iso_month;
  GET_FROM_DATE_AND_YEAR_MONTH(iso_month, Smi)
  // 6. Return ! ISOMonth(temporalDateLike).
  return Handle<Smi>(Smi::FromInt(iso_month), isolate);
}

// #sec-temporal.calendar.prototype.monthcode
MaybeHandle<String> JSTemporalCalendar::MonthCode(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> temporal_date_like) {
  const char* method = "Temporal.Calendar.prototype.monthCode";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]],
  // [[InitializedTemporalMonthDay]], or
  // [[InitializedTemporalYearMonth]] internal slot, then
  int32_t iso_month;
  GET_FROM_DATE_AND_YEAR_MONTH_AND_MONTH_DAY(iso_month, String)
  // 5. Return ! ISOMonthCode(temporalDateLike).
  std::ostringstream month_code;
  month_code << "M" << std::setfill('0') << std::setw(2) << iso_month;
  return isolate->factory()->NewStringFromAsciiChecked(
      month_code.str().c_str());
}

// #sec-temporal.calendar.prototype.day
MaybeHandle<Smi> JSTemporalCalendar::Day(Isolate* isolate,
                                         Handle<JSTemporalCalendar> calendar,
                                         Handle<Object> temporal_date_like) {
  const char* method = "Temporal.Calendar.prototype.day";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]] or [[InitializedTemporalMonthDay]]
  // internal slot, then
  int32_t iso_day;
  GET_FROM_DATE_AND_MONTH_DAY(iso_day, Smi)
  // 6. Return ! ISOMonth(temporalDateLike).
  return Handle<Smi>(Smi::FromInt(iso_day), isolate);
}

#define TO_ISO(name, Name)                                                   \
  MaybeHandle<Smi> JSTemporalCalendar::Name(                                 \
      Isolate* isolate, Handle<JSTemporalCalendar> calendar,                 \
      Handle<Object> temporal_date_like) {                                   \
    const char* method = "Temporal.Calendar.prototype" #name;                \
    /* 1. Let calendar be the this value. */                                 \
    /* 2. Perform ? RequireInternalSlot(calendar, */                         \
    /* [[InitializedTemporalCalendar]]). */                                  \
    /* 3. Assert: calendar.[[Identifier]] is "iso8601". */                   \
    /* 4. Let temporalDate be ? ToTemporalDate(temporalDateLike). */         \
    Handle<JSTemporalPlainDate> date;                                        \
    ASSIGN_RETURN_ON_EXCEPTION(                                              \
        isolate, date,                                                       \
        temporal::ToTemporalDate(isolate, temporal_date_like, method), Smi); \
    /* 5. Return 𝔽(! ToISO # Name(temporalDate.[[ISOYear]], */            \
    /* temporalDate.[[ISOMonth]], temporalDate.[[ISODay]])). */              \
    return Handle<Smi>(                                                      \
        Smi::FromInt(temporal::ToISO##Name(                                  \
            isolate, date->iso_year(), date->iso_month(), date->iso_day())), \
        isolate);                                                            \
  }

// #sec-temporal.calendar.prototype.dayofweek
TO_ISO(dayOfWeek, DayOfWeek)
// #sec-temporal.calendar.prototype.dayofyear
TO_ISO(dayOfYear, DayOfYear)
// #sec-temporal.calendar.prototype.weekofyear
TO_ISO(weekOfYear, WeekOfYear)

#undef TO_ISO

// #sec-temporal.calendar.prototype.daysinweek
MaybeHandle<Smi> JSTemporalCalendar::DaysInWeek(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> temporal_date_like) {
  const char* method = "Temporal.Calendar.prototype.daysInWeek";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. Perform ? ToTemporalDate(temporalDateLike).
  Handle<JSTemporalPlainDate> date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date,
      temporal::ToTemporalDate(isolate, temporal_date_like, method), Smi);
  // 5. Return 7𝔽.
  return Handle<Smi>(Smi::FromInt(7), isolate);
}

// #sec-temporal.calendar.prototype.daysinmonth
MaybeHandle<Smi> JSTemporalCalendar::DaysInMonth(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> temporal_date_like) {
  const char* method = "Temporal.Calendar.prototype.daysInMonth";
  // 1 Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  int32_t iso_year;
  int32_t iso_month;
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]] or [[InitializedTemporalYearMonth]]
  // internal slots, then
  if (temporal_date_like->IsJSTemporalPlainDate()) {
    iso_year =
        Handle<JSTemporalPlainDate>::cast(temporal_date_like)->iso_year();
    iso_month =
        Handle<JSTemporalPlainDate>::cast(temporal_date_like)->iso_month();
  } else if (temporal_date_like->IsJSTemporalPlainYearMonth()) {
    iso_year =
        Handle<JSTemporalPlainYearMonth>::cast(temporal_date_like)->iso_year();
    iso_month =
        Handle<JSTemporalPlainYearMonth>::cast(temporal_date_like)->iso_month();
  } else {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    Handle<JSTemporalPlainDate> date;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, date,
        temporal::ToTemporalDate(isolate, temporal_date_like, method), Smi);
    iso_year = date->iso_year();
    iso_month = date->iso_month();
  }
  // 5. Return 𝔽(! ISODaysInMonth(temporalDateLike.[[ISOYear]],
  // temporalDateLike.[[ISOMonth]])).
  return Handle<Smi>(
      Smi::FromInt(temporal::ISODaysInMonth(isolate, iso_year, iso_month)),
      isolate);
}

// #sec-temporal.calendar.prototype.daysinyear
MaybeHandle<Smi> JSTemporalCalendar::DaysInYear(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> temporal_date_like) {
  const char* method = "Temporal.Calendar.prototype.daysInYear";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  int32_t iso_year;
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]] or
  // [[InitializedTemporalYearMonth]] internal slot, then
  GET_FROM_DATE_AND_YEAR_MONTH(iso_year, Smi)
  // 5. Return 𝔽(! ISODaysInYear(temporalDateLike.[[ISOYear]])).
  return Handle<Smi>(Smi::FromInt(temporal::ISODaysInYear(isolate, iso_year)),
                     isolate);
}

// #sec-temporal.calendar.prototype.monthsinyear
MaybeHandle<Smi> JSTemporalCalendar::MonthsInYear(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> temporal_date_like) {
  const char* method = "Temporal.Calendar.prototype.monthsInYear";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]], or
  // [[InitializedTemporalYearMonth]] internal slot, then
  if (!(temporal_date_like->IsJSTemporalPlainDate() ||
        temporal_date_like->IsJSTemporalPlainDateTime() ||
        temporal_date_like->IsJSTemporalPlainYearMonth())) {
    // a. Perform ? ToTemporalDate(temporalDateLike).
    Handle<JSTemporalPlainDate> date;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, date,
        temporal::ToTemporalDate(isolate, temporal_date_like, method), Smi);
  }
  // 5. Return 12𝔽.
  return Handle<Smi>(Smi::FromInt(12), isolate);
}

// #sec-temporal.calendar.prototype.inleapyear
MaybeHandle<Oddball> JSTemporalCalendar::InLeapYear(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> temporal_date_like) {
  const char* method = "Temporal.Calendar.prototype.inLeapYear";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  int32_t iso_year;
  // 4. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]], or
  // [[InitializedTemporalYearMonth]] internal slot, then
  GET_FROM_DATE_AND_YEAR_MONTH(iso_year, Oddball)
  // 5. Return 𝔽(! IsISOLeapYear(temporalDateLike.[[ISOYear]])).
  Factory* factory = isolate->factory();
  return temporal::IsISOLeapYear(isolate, iso_year) ? factory->true_value()
                                                    : factory->false_value();
}

#ifdef V8_INTL_SUPPORT
// #sec-temporal.calendar.prototype.era
MaybeHandle<Object> JSTemporalCalendar::Era(Isolate* isolate,
                                            Handle<JSTemporalCalendar> calendar,
                                            Handle<Object> temporal_date_like) {
  const char* method = "Temporal.Calendar.prototype.era";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]], [[InitializedTemporalDateTime]], or
  // [[InitializedTemporalYearMonth]] internal slot, then
  Handle<JSTemporalPlainDate> date;
  if (!(temporal_date_like->IsJSReceiver() ||
        temporal_date_like->IsJSTemporalPlainDate() ||
        temporal_date_like->IsJSTemporalPlainDateTime() ||
        temporal_date_like->IsJSTemporalPlainYearMonth())) {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, date,
        temporal::ToTemporalDate(isolate, temporal_date_like, method), Smi);
  }
  // 4. If calendar.[[Identifier]] is "iso8601", then
  if (calendar->calendar_index() == 0) {
    // a. Return undefined.
    return isolate->factory()->undefined_value();
  } else {
    // 5. Let era be the result of implementation-defined processing of
    // temporalDateLike and the value of calendar.[[Identifier]].
    // 6. Return era.
    return isolate->factory()->undefined_value();
  }
}

// #sec-temporal.calendar.prototype.erayear
MaybeHandle<Object> JSTemporalCalendar::EraYear(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> temporal_date_like) {
  const char* method = "Temporal.Calendar.prototype.eraYear";
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. If Type(temporalDateLike) is not Object or temporalDateLike does not
  // have an [[InitializedTemporalDate]],  [[InitializedTemporalDateTime]], or
  // [[InitializedTemporalYearMonth]] internal slot, then
  Handle<JSTemporalPlainDate> date;
  if (!(temporal_date_like->IsJSReceiver() ||
        temporal_date_like->IsJSTemporalPlainDate() ||
        temporal_date_like->IsJSTemporalPlainDateTime() ||
        temporal_date_like->IsJSTemporalPlainYearMonth())) {
    // a. Set temporalDateLike to ? ToTemporalDate(temporalDateLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, date,
        temporal::ToTemporalDate(isolate, temporal_date_like, method), Smi);
  }
  // 4. If calendar.[[Identifier]] is "iso8601", then
  if (calendar->calendar_index() == 0) {
    // a. Return undefined.
    return isolate->factory()->undefined_value();
  } else {
    // 5. Let eraYear be the result of implementation-defined processing of
    // temporalDateLike and the value of calendar.[[Identifier]].
    // 6. Return 𝔽(eraYear).
    return isolate->factory()->undefined_value();
  }
}
#endif  //  V8_INTL_SUPPORT

// #sec-temporal.calendar.prototype.mergefields
MaybeHandle<JSReceiver> JSTemporalCalendar::MergeFields(
    Isolate* isolate, Handle<JSTemporalCalendar> calendar,
    Handle<Object> fields_obj, Handle<Object> additional_fields_obj) {
  // 1. Let calendar be the this value.
  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  // 3. Assert: calendar.[[Identifier]] is "iso8601".
  // 4. Set fields to ? ToObject(fields).
  Handle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             Object::ToObject(isolate, fields_obj), JSReceiver);

  // 5. Set additionalFields to ? ToObject(additionalFields).
  Handle<JSReceiver> additional_fields;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, additional_fields,
                             Object::ToObject(isolate, additional_fields_obj),
                             JSReceiver);
  // 6. Return ? DefaultMergeFields(fields, additionalFields).
  return temporal::DefaultMergeFields(isolate, fields, additional_fields);
}

// #sec-temporal.calendar.prototype.tostring
Handle<String> JSTemporalCalendar::ToString(Isolate* isolate,
                                            Handle<JSTemporalCalendar> calendar,
                                            const char* method) {
  return temporal::CalendarIdentifier(isolate, calendar->calendar_index());
}

// #sec-temporal.now.timezone
MaybeHandle<JSTemporalTimeZone> JSTemporalTimeZone::Now(Isolate* isolate) {
  return temporal::SystemTimeZone(isolate);
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

// #sec-temporal.timezone.from
MaybeHandle<JSReceiver> JSTemporalTimeZone::From(Isolate* isolate,
                                                 Handle<Object> item) {
  return temporal::ToTemporalTimeZone(isolate, item, "Temporal.TimeZone.from");
}

namespace {

#ifdef V8_INTL_SUPPORT
MaybeHandle<Object> GetIANATimeZoneOffsetNanoseconds(Isolate* isolate,
                                                     Handle<BigInt> nanoseconds,
                                                     int32_t time_zone_index) {
  if (time_zone_index == 0) {
    return isolate->factory()->NewNumberFromInt64(0);
  }
  Handle<BigInt> time_in_milliseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_in_milliseconds,
      BigInt::Divide(isolate, nanoseconds,
                     BigInt::FromUint64(isolate, 1000000)),
      Object);

  std::string id = Intl::TimeZoneIdFromIndex(time_zone_index);

  Maybe<int64_t> maybe_offset_in_milliseconds =
      Intl::GetTimeZoneOffsetMilliseconds(isolate, id,
                                          time_in_milliseconds->AsInt64());
  MAYBE_RETURN(maybe_offset_in_milliseconds, Handle<Object>());

  return isolate->factory()->NewNumberFromInt64(
      1000000L * maybe_offset_in_milliseconds.FromJust());
}

MaybeHandle<Object> GetIANATimeZoneNextTransition(Isolate* isolate,
                                                  Handle<BigInt> nanoseconds,
                                                  int32_t time_zone_index) {
  return isolate->factory()->null_value();
}

MaybeHandle<Object> GetIANATimeZonePreviousTransition(
    Isolate* isolate, Handle<BigInt> nanoseconds, int32_t time_zone_index) {
  return isolate->factory()->null_value();
}

// #sec-temporal.timezone.prototype.getpossibleinstantsfor
// #sec-temporal-getianatimezoneepochvalue
MaybeHandle<JSArray> GetIANATimeZoneEpochValueAsArrayOfInstant(
    Isolate* isolate, int32_t time_zone_id, int32_t year, int32_t month,
    int32_t day, int32_t hour, int32_t minute, int32_t second,
    int32_t millisecond, int32_t microsecond, int32_t nanosecond) {
  Factory* factory = isolate->factory();

  // 6. Let possibleInstants be a new empty List.
  Handle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, epoch_nanoseconds,
                             temporal::GetEpochFromISOParts(
                                 isolate, year, month, day, hour, minute,
                                 second, millisecond, microsecond, nanosecond),
                             JSArray);
  Handle<FixedArray> fixed_array = factory->NewFixedArray(1);
  // 7. For each value epochNanoseconds in possibleEpochNanoseconds, do
  // a. Let instant be ! CreateTemporalInstant(epochNanoseconds).

  // TODO(ftang) Handle 0 and 2 items.
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(isolate, epoch_nanoseconds), JSArray);
  // b. Append instant to possibleInstants.
  fixed_array->set(0, *(instant));

  // 8. Return ! CreateArrayFromList(possibleInstants).
  return factory->NewJSArrayWithElements(fixed_array);
}

#else  // V8_INTL_SUPPORT

MaybeHandle<Object> GetIANATimeZoneOffsetNanoseconds(Isolate* isolate,
                                                     Handle<BigInt> nanoseconds,
                                                     int32_t time_zone_index) {
  CHECK_EQ(time_zone_index, 0);
  return isolate->factory()->NewNumberFromInt64(0);
}

MaybeHandle<Object> GetIANATimeZoneNextTransition(Isolate* isolate,
                                                  Handle<BigInt> nanoseconds,
                                                  int32_t time_zone_index) {
  return isolate->factory()->null_value();
}

MaybeHandle<Object> GetIANATimeZonePreviousTransition(
    Isolate* isolate, Handle<BigInt> nanoseconds, int32_t time_zone_index) {
  return isolate->factory()->null_value();
}

// #sec-temporal.timezone.prototype.getpossibleinstantsfor
// #sec-temporal-getianatimezoneepochvalue
MaybeHandle<JSArray> GetIANATimeZoneEpochValueAsArrayOfInstant(
    Isolate* isolate, int32_t time_zone_id, int32_t year, int32_t month,
    int32_t day, int32_t hour, int32_t minute, int32_t second,
    int32_t millisecond, int32_t microsecond, int32_t nanosecond) {
  Factory* factory = isolate->factory();
  // 6. Let possibleInstants be a new empty List.
  Handle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, epoch_nanoseconds,
                             temporal::GetEpochFromISOParts(
                                 isolate, year, month, day, hour, minute,
                                 second, millisecond, microsecond, nanosecond),
                             JSArray);
  Handle<FixedArray> fixed_array = factory->NewFixedArray(1);
  // 7. For each value epochNanoseconds in possibleEpochNanoseconds, do
  // a. Let instant be ! CreateTemporalInstant(epochNanoseconds).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(isolate, epoch_nanoseconds), JSArray);
  // b. Append instant to possibleInstants.
  fixed_array->set(0, *(instant));

  // 8. Return ! CreateArrayFromList(possibleInstants).
  return factory->NewJSArrayWithElements(fixed_array);
}

#endif  // V8_INTL_SUPPORT

}  // namespace

// #sec-temporal.timezone.prototype.getoffsetnanosecondsfor
MaybeHandle<Object> JSTemporalTimeZone::GetOffsetNanosecondsFor(
    Isolate* isolate, Handle<JSTemporalTimeZone> time_zone,
    Handle<Object> instant_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.TimeZone.prototype.getOffsetNanosecondsFor";
  // 1. Let timeZone be the this value.
  // 2. Perform ? RequireInternalSlot(timeZone,
  // [[InitializedTemporalTimeZone]]).
  // 3. Set instant to ? ToTemporalInstant(instant).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::ToTemporalInstant(isolate, instant_obj, method), Smi);
  // 4. If timeZone.[[OffsetNanoseconds]] is not undefined, return
  // timeZone.[[OffsetNanoseconds]].
  if (time_zone->is_offset()) {
    Handle<Object> result = isolate->factory()->NewNumberFromInt64(
        time_zone->offset_nanoseconds());
    return result;
  }
  // 5. Return ! GetIANATimeZoneOffsetNanoseconds(instant.[[Nanoseconds]],
  // timeZone.[[Identifier]]).
  return GetIANATimeZoneOffsetNanoseconds(
      isolate, Handle<BigInt>(instant->nanoseconds(), isolate),
      time_zone->time_zone_index());
}

// #sec-temporal.timezone.prototype.getoffsetstringfor
MaybeHandle<String> JSTemporalTimeZone::GetOffsetStringFor(
    Isolate* isolate, Handle<JSTemporalTimeZone> time_zone,
    Handle<Object> instant_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.TimeZone.prototype.getOffsetStringFor";
  // 1. Let timeZone be the this value.
  // 2. Perform ? RequireInternalSlot(timeZone,
  // [[InitializedTemporalTimeZone]]).
  // 3. Set instant to ? ToTemporalInstant(instant).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::ToTemporalInstant(isolate, instant_obj, method), String);
  // 4. Return ? BuiltinTimeZoneGetOffsetStringFor(timeZone, instant).
  return temporal::BuiltinTimeZoneGetOffsetStringFor(isolate, time_zone,
                                                     instant);
}

// #sec-temporal.timezone.prototype.getplaindatetimefor
MaybeHandle<JSTemporalPlainDateTime> JSTemporalTimeZone::GetPlainDateTimeFor(
    Isolate* isolate, Handle<JSTemporalTimeZone> time_zone,
    Handle<Object> instant_obj, Handle<Object> calendar_like) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.TimeZone.prototype.getPlainDateTimeFor";
  // TODO(ftang) See https://github.com/tc39/proposal-temporal/issues/1692
  // 1. Let timeZone be the this value.
  // 2. Set instant to ? ToTemporalInstant(instant).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::ToTemporalInstant(isolate, instant_obj, method),
      JSTemporalPlainDateTime);
  // 3. Let calendar be ? ToTemporalCalendarWithISODefault(calendarLike).
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             temporal::ToTemporalCalendarWithISODefault(
                                 isolate, calendar_like, method),
                             JSTemporalPlainDateTime);

  // 4. Return ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant,
  // calendar).
  return temporal::BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone,
                                                      instant, calendar);
}

// #sec-temporal.timezone.prototype.getinstantfor
MaybeHandle<JSTemporalInstant> JSTemporalTimeZone::GetInstantFor(
    Isolate* isolate, Handle<JSTemporalTimeZone> time_zone,
    Handle<Object> date_time_obj, Handle<Object> options_obj) {
  const char* method = "Temporal.TimeZone.prototype.getInstantFor";
  // 1. Let timeZone be the this value.
  // 2. Perform ? RequireInternalSlot(timeZone,
  // [[InitializedTemporalTimeZone]]).
  // 3. Set dateTime to ? ToTemporalDateTime(dateTime).
  Handle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      temporal::ToTemporalDateTime(isolate, date_time_obj, method),
      JSTemporalInstant);

  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalInstant);

  // 5. Let disambiguation be ? ToTemporalDisambiguation(options).
  Maybe<temporal::Disambiguation> maybe_disambiguation =
      temporal::ToTemporalDisambiguation(isolate, options, method);
  MAYBE_RETURN(maybe_disambiguation, Handle<JSTemporalInstant>());
  temporal::Disambiguation disambiguation = maybe_disambiguation.FromJust();

  // 6. Return ? BuiltinTimeZoneGetInstantFor(timeZone, dateTime,
  // disambiguation).
  return temporal::BuiltinTimeZoneGetInstantFor(isolate, time_zone, date_time,
                                                disambiguation);
}

// #sec-temporal.timezone.prototype.getpossibleinstantsfor
MaybeHandle<JSArray> JSTemporalTimeZone::GetPossibleInstantsFor(
    Isolate* isolate, Handle<JSTemporalTimeZone> time_zone,
    Handle<Object> date_time_obj) {
  const char* method = "Temporal.TimeZone.prototype.getPossibleInstantsFor";
  Factory* factory = isolate->factory();
  // 1. Let timeZone be the this value.
  // 2. Perform ? RequireInternalSlot(timeZone,
  // [[InitializedTemporalTimezone]]).
  // 3. Set dateTime to ? ToTemporalDateTime(dateTime).
  Handle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      temporal::ToTemporalDateTime(isolate, date_time_obj, method), JSArray);
  // 4. If timeZone.[[OffsetNanoseconds]] is not undefined, then
  if (time_zone->is_offset()) {
    Handle<BigInt> epoch_nanseconds;
    // a. Let epochNanoseconds be ! GetEpochFromISOParts(dateTime.[[ISOYear]],
    // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
    // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
    // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
    // dateTime.[[ISONanosecond]]).
    Handle<BigInt> epoch_nanoseconds;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, epoch_nanoseconds,
        temporal::GetEpochFromISOParts(
            isolate, date_time->iso_year(), date_time->iso_month(),
            date_time->iso_day(), date_time->iso_hour(),
            date_time->iso_minute(), date_time->iso_second(),
            date_time->iso_millisecond(), date_time->iso_microsecond(),
            date_time->iso_nanosecond()),
        JSArray);
    // b. Let instant be ! CreateTemporalInstant(epochNanoseconds −
    // timeZone.[[OffsetNanoseconds]]).
    Handle<BigInt> diff;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, diff,
        BigInt::Subtract(
            isolate, epoch_nanoseconds,
            BigInt::FromInt64(isolate, time_zone->offset_nanoseconds())),
        JSArray);
    Handle<JSTemporalInstant> instant;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, instant,
                               temporal::CreateTemporalInstant(isolate, diff),
                               JSArray);
    // c. Return ! CreateArrayFromList(« instant »).
    Handle<FixedArray> fixed_array = factory->NewFixedArray(1);
    fixed_array->set(0, *(instant));
    return factory->NewJSArrayWithElements(fixed_array);
  }

  // 5. Let possibleEpochNanoseconds be ?
  // GetIANATimeZoneEpochValue(timeZone.[[Identifier]], dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]]).

  // ... Step 5-8 put into GetIANATimeZoneEpochValueAsArrayOfInstant
  // 8. Return ! CreateArrayFromList(possibleInstants).
  return GetIANATimeZoneEpochValueAsArrayOfInstant(
      isolate, time_zone->time_zone_index(), date_time->iso_year(),
      date_time->iso_month(), date_time->iso_day(), date_time->iso_hour(),
      date_time->iso_minute(), date_time->iso_second(),
      date_time->iso_millisecond(), date_time->iso_microsecond(),
      date_time->iso_nanosecond());
}

// #sec-temporal.timezone.prototype.getnexttransition
MaybeHandle<Object> JSTemporalTimeZone::GetNextTransition(
    Isolate* isolate, Handle<JSTemporalTimeZone> time_zone,
    Handle<Object> starting_point_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.TimeZone.prototype.getNextTransition";
  // 1. Let timeZone be the this value.
  // 2. Perform ? RequireInternalSlot(timeZone,
  // [[InitializedTemporalTimeZone]]).
  // 3. Set startingPoint to ? ToTemporalInstant(startingPoint).
  Handle<JSTemporalInstant> starting_point;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, starting_point,
      temporal::ToTemporalInstant(isolate, starting_point_obj, method), Object);
  // 4. If timeZone.[[OffsetNanoseconds]] is not undefined, return null.
  if (time_zone->is_offset()) {
    return isolate->factory()->null_value();
  }
  // 5. Let transition be ?
  // GetIANATimeZoneNextTransition(startingPoint.[[Nanoseconds]],
  // timeZone.[[Identifier]]).
  Handle<Object> transition_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, transition_obj,
      GetIANATimeZoneNextTransition(
          isolate, Handle<BigInt>(starting_point->nanoseconds(), isolate),
          time_zone->time_zone_index()),
      Object);
  // 6. If transition is null, return null.
  if (transition_obj->IsNull()) {
    return isolate->factory()->null_value();
  }
  CHECK(transition_obj->IsBigInt());
  Handle<BigInt> transition = Handle<BigInt>::cast(transition_obj);
  // 7. Return ! CreateTemporalInstant(transition).
  return temporal::CreateTemporalInstant(isolate, transition);
}

// #sec-temporal.timezone.prototype.getprevioustransition
MaybeHandle<Object> JSTemporalTimeZone::GetPreviousTransition(
    Isolate* isolate, Handle<JSTemporalTimeZone> time_zone,
    Handle<Object> starting_point_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.TimeZone.prototype.getPreviousTransition";
  // 1. Let timeZone be the this value.
  // 2. Perform ? RequireInternalSlot(timeZone,
  // [[InitializedTemporalTimeZone]]).
  // 3. Set startingPoint to ? ToTemporalInstant(startingPoint).
  Handle<JSTemporalInstant> starting_point;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, starting_point,
      temporal::ToTemporalInstant(isolate, starting_point_obj, method), Object);
  // 4. If timeZone.[[OffsetNanoseconds]] is not undefined, return null.
  if (time_zone->is_offset()) {
    return isolate->factory()->null_value();
  }
  // 5. Let transition be ?
  // GetIANATimeZonePreviousTransition(startingPoint.[[Nanoseconds]],
  // timeZone.[[Identifier]]).
  Handle<Object> transition_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, transition_obj,
      GetIANATimeZonePreviousTransition(
          isolate, Handle<BigInt>(starting_point->nanoseconds(), isolate),
          time_zone->time_zone_index()),
      Object);
  // 6. If transition is null, return null.
  if (transition_obj->IsNull()) {
    return isolate->factory()->null_value();
  }
  CHECK(transition_obj->IsBigInt());
  Handle<BigInt> transition = Handle<BigInt>::cast(transition_obj);
  // 7. Return ! CreateTemporalInstant(transition).
  return temporal::CreateTemporalInstant(isolate, transition);
}

// #sec-temporal.timezone.prototype.tostring
Handle<Object> JSTemporalTimeZone::ToString(
    Isolate* isolate, Handle<JSTemporalTimeZone> time_zone,
    const char* method) {
  return isolate->factory()->NewStringFromAsciiChecked(time_zone->id().c_str());
}

int32_t JSTemporalTimeZone::time_zone_index() const {
  CHECK(is_offset() == false);
  return offset_milliseconds_or_time_zone_index();
}

int64_t JSTemporalTimeZone::offset_nanoseconds() const {
  TEMPORAL_ENTER_FUNC
  CHECK(is_offset());
  return 1000000L * offset_milliseconds() + offset_sub_milliseconds();
}

void JSTemporalTimeZone::set_offset_nanoseconds(int64_t ns) {
  this->set_offset_milliseconds(static_cast<int32_t>(ns / 1000000L));
  this->set_offset_sub_milliseconds(static_cast<int32_t>(ns % 1000000L));
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

// #sec-temporal.now.plaindate
MaybeHandle<JSTemporalPlainDate> JSTemporalPlainDate::Now(
    Isolate* isolate, Handle<Object> calendar,
    Handle<Object> temporal_time_zone_like) {
  const char* method = "Temporal.Now.plainDate";
  // 1. Let dateTime be ? SystemDateTime(temporalTimeZoneLike, calendar).
  Handle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      temporal::SystemDateTime(isolate, temporal_time_zone_like, calendar,
                               method),
      JSTemporalPlainDate);
  // 2. Return ! CreateTemporalDate(dateTime.[[ISOYear]], dateTime.[[ISOMonth]],
  // dateTime.[[ISODay]], dateTime.[[Calendar]]).
  return temporal::CreateTemporalDate(
      isolate, date_time->iso_year(), date_time->iso_month(),
      date_time->iso_day(), Handle<JSReceiver>(date_time->calendar(), isolate));
}

// #sec-temporal.now.plaindateiso
MaybeHandle<JSTemporalPlainDate> JSTemporalPlainDate::NowISO(
    Isolate* isolate, Handle<Object> temporal_time_zone_like) {
  const char* method = "Temporal.Now.plainDateISO";
  // 1. Let calendar be ! GetISO8601Calendar().
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             temporal::GetISO8601Calendar(isolate),
                             JSTemporalPlainDate);
  // 2. Let dateTime be ? SystemDateTime(temporalTimeZoneLike, calendar).
  Handle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      temporal::SystemDateTime(isolate, temporal_time_zone_like, calendar,
                               method),
      JSTemporalPlainDate);
  // 3. Return ! CreateTemporalDate(dateTime.[[ISOYear]], dateTime.[[ISOMonth]],
  // dateTime.[[ISODay]], dateTime.[[Calendar]]).
  return temporal::CreateTemporalDate(
      isolate, date_time->iso_year(), date_time->iso_month(),
      date_time->iso_day(), Handle<JSReceiver>(date_time->calendar(), isolate));
}

// #sec-temporal.plaindate.from
MaybeHandle<JSTemporalPlainDate> JSTemporalPlainDate::From(
    Isolate* isolate, Handle<Object> item, Handle<Object> options_obj) {
  const char* method = "Temporal.PlainDate.from";
  // 1. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainDate);
  // 2. If Type(item) is Object and item has an [[InitializedTemporalDate]]
  // internal slot, then
  if (item->IsJSTemporalPlainDate()) {
    // a. Perform ? ToTemporalOverflow(options).
    Maybe<temporal::ShowOverflow> maybe_overflow =
        temporal::ToTemporalOverflow(isolate, options, method);
    MAYBE_RETURN(maybe_overflow, Handle<JSTemporalPlainDate>());
    // b. Return ? CreateTemporalDate(item.[[ISOYear]], item.[[ISOMonth]],
    // item.[[ISODay]], item.[[Calendar]]).
    Handle<JSTemporalPlainDate> date = Handle<JSTemporalPlainDate>::cast(item);
    return temporal::CreateTemporalDate(
        isolate, date->iso_year(), date->iso_month(), date->iso_day(),
        Handle<JSReceiver>(date->calendar(), isolate));
  }
  // 3. Return ? ToTemporalDate(item, options).
  return temporal::ToTemporalDate(isolate, item, options, method);
}

// #sec-temporal.plaindate.compare
MaybeHandle<Smi> JSTemporalPlainDate::Compare(Isolate* isolate,
                                              Handle<Object> one_obj,
                                              Handle<Object> two_obj) {
  const char* method = "Temporal.PlainDate.compare";
  // 1. Set one to ? ToTemporalDate(one).
  Handle<JSTemporalPlainDate> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one, temporal::ToTemporalDate(isolate, one_obj, method), Smi);
  // 2. Set two to ? ToTemporalDate(two).
  Handle<JSTemporalPlainDate> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two, temporal::ToTemporalDate(isolate, two_obj, method), Smi);
  // 3. Return 𝔽(! CompareISODate(one.[[ISOYear]], one.[[ISOMonth]],
  // one.[[ISODay]], two.[[ISOYear]], two.[[ISOMonth]], two.[[ISODay]])).
  return Handle<Smi>(
      Smi::FromInt(temporal::CompareISODate(
          isolate, one->iso_year(), one->iso_month(), one->iso_day(),
          two->iso_year(), two->iso_month(), two->iso_day())),
      isolate);
}

#define IMPL_TO_PLAIN(Name, r1, r2)                                           \
  MaybeHandle<JSTemporalPlain##Name> JSTemporalPlainDate::ToPlain##Name(      \
      Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date) {          \
    Factory* factory = isolate->factory();                                    \
    /* 1. Let temporalDate be the this value.         */                      \
    /* 2. Perform ? RequireInternalSlot(temporalDate, */                      \
    /* [[InitializedTemporalDate]]).                  */                      \
    /* 3. Let calendar be temporalDate.[[Calendar]].  */                      \
    Handle<JSReceiver> calendar =                                             \
        Handle<JSReceiver>(temporal_date->calendar(), isolate);               \
    /* 4. Let fieldNames be ? CalendarFields(calendar, « #r1 , #r2 »). */   \
    Handle<FixedArray> field_names = factory->NewFixedArray(2);               \
    field_names->set(0, *(factory->r1##_string()));                           \
    field_names->set(1, *(factory->r2##_string()));                           \
    ASSIGN_RETURN_ON_EXCEPTION(                                               \
        isolate, field_names,                                                 \
        temporal::CalendarFields(isolate, calendar, field_names),             \
        JSTemporalPlain##Name);                                               \
    /* 5. Let fields be ? PrepareTemporalFields(temporalDate, fieldNames,     \
     * «»).*/                                                               \
    Handle<JSReceiver> fields;                                                \
    ASSIGN_RETURN_ON_EXCEPTION(                                               \
        isolate, fields,                                                      \
        temporal::PrepareTemporalFields(isolate, temporal_date, field_names,  \
                                        false, false),                        \
        JSTemporalPlain##Name);                                               \
    /* 6. Return ? Name## FromFields(calendar, fields). */                    \
    return temporal::Name##FromFields(isolate, calendar, fields,              \
                                      isolate->factory()->undefined_value()); \
  }

// #sec-temporal.plaindate.prototype.toplainyearmonth
IMPL_TO_PLAIN(YearMonth, monthCode, year)
// #sec-temporal.plaindate.prototype.toplainmonthday
IMPL_TO_PLAIN(MonthDay, day, monthCode)
#undef IMPL_TO_PLAIN

// #sec-temporal.plaindate.prototype.toplaindatetime
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainDate::ToPlainDateTime(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date,
    Handle<Object> temporal_time_obj) {
  const char* method = "Temporal.PlainDate.prototype.toPlainDateTime";
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. If temporalTime is undefined, then
  if (temporal_time_obj->IsUndefined()) {
    // a. Return ? CreateTemporalDateTime(temporalDate.[[ISOYear]],
    // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]], 0, 0, 0, 0, 0, 0,
    // temporalDate.[[Calendar]]).
    return temporal::CreateTemporalDateTime(
        isolate, temporal_date->iso_year(), temporal_date->iso_month(),
        temporal_date->iso_day(), 0, 0, 0, 0, 0, 0,
        Handle<JSReceiver>(temporal_date->calendar(), isolate));
  }
  // 4. Set temporalTime to ? ToTemporalTime(temporalTime).
  Handle<JSTemporalPlainTime> temporal_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_time,
      temporal::ToTemporalTime(isolate, temporal_time_obj,
                               temporal::ShowOverflow::kConstrain, method),
      JSTemporalPlainDateTime);
  // 5. Return ? CreateTemporalDateTime(temporalDate.[[ISOYear]],
  // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]],
  // temporalTime.[[ISOHour]], temporalTime.[[ISOMinute]],
  // temporalTime.[[ISOSecond]], temporalTime.[[ISOMillisecond]],
  // temporalTime.[[ISOMicrosecond]], temporalTime.[[ISONanosecond]],
  // temporalDate.[[Calendar]]).
  return temporal::CreateTemporalDateTime(
      isolate, temporal_date->iso_year(), temporal_date->iso_month(),
      temporal_date->iso_day(), temporal_time->iso_hour(),
      temporal_time->iso_minute(), temporal_time->iso_second(),
      temporal_time->iso_millisecond(), temporal_time->iso_microsecond(),
      temporal_time->iso_nanosecond(),
      Handle<JSReceiver>(temporal_date->calendar(), isolate));
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

// #sec-temporal.plaindate.prototype.add
MaybeHandle<JSTemporalPlainDate> JSTemporalPlainDate::Add(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date,
    Handle<Object> temporal_duration_like, Handle<Object> options_obj) {
  const char* method = "Temporal.PlainDate.prototype.add";
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Let duration be ? ToTemporalDuration(temporalDurationLike).
  Handle<JSTemporalDuration> duration;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, duration,
      temporal::ToTemporalDuration(isolate, temporal_duration_like, method),
      JSTemporalPlainDate);

  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainDate);
  // 5. Return ? CalendarDateAdd(temporalDate.[[Calendar]], temporalDate,
  // duration, options).
  return temporal::CalendarDateAdd(
      isolate, Handle<JSReceiver>(temporal_date->calendar(), isolate),
      temporal_date, duration, options, isolate->factory()->undefined_value());
}

// #sec-temporal.plaindate.prototype.subtract
MaybeHandle<JSTemporalPlainDate> JSTemporalPlainDate::Subtract(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date,
    Handle<Object> temporal_duration_like, Handle<Object> options_obj) {
  const char* method = "Temporal.PlainDate.prototype.subtract";
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Let duration be ? ToTemporalDuration(temporalDurationLike).
  Handle<JSTemporalDuration> duration;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, duration,
      temporal::ToTemporalDuration(isolate, temporal_duration_like, method),
      JSTemporalPlainDate);

  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainDate);

  // 5. Let negatedDuration be ! CreateNegatedTemporalDuration(duration).
  Handle<JSTemporalDuration> negated_duration;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, negated_duration,
      temporal::CreateNegatedTemporalDuration(isolate, duration),
      JSTemporalPlainDate);

  // 6. Return ? CalendarDateAdd(temporalDate.[[Calendar]], temporalDate,
  // negatedDuration, options).
  return temporal::CalendarDateAdd(
      isolate, Handle<JSReceiver>(temporal_date->calendar(), isolate),
      temporal_date, negated_duration, options,
      isolate->factory()->undefined_value());
}

// #sec-temporal.plaindate.prototype.with
MaybeHandle<JSTemporalPlainDate> JSTemporalPlainDate::With(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date,
    Handle<Object> temporal_date_like_obj, Handle<Object> options_obj) {
  const char* method = "Temporal.PlainDate.prototype.with";
  Factory* factory = isolate->factory();
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. If Type(temporalDateLike) is not Object, then
  if (!temporal_date_like_obj->IsJSReceiver()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainDate);
  }
  Handle<JSReceiver> temporal_date_like =
      Handle<JSReceiver>::cast(temporal_date_like_obj);
  // 4. Perform ? RejectTemporalCalendarType(temporalDateLike).
  Maybe<bool> maybe_reject =
      temporal::RejectTemporalCalendarType(isolate, temporal_date_like);

  MAYBE_RETURN(maybe_reject, Handle<JSTemporalPlainDate>());

#define THROW_IF_NOT_UNDEFINED(obj, name)                                \
  {                                                                      \
    Handle<Object> prop;                                                 \
    ASSIGN_RETURN_ON_EXCEPTION(                                          \
        isolate, prop,                                                   \
        JSReceiver::GetProperty(isolate, obj, factory->name##_string()), \
        JSTemporalPlainDate);                                            \
    if (!prop->IsUndefined()) {                                          \
      THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),     \
                      JSTemporalPlainDate);                              \
    }                                                                    \
  }
  // 5. Let calendarProperty be ? Get(temporalDateLike, "calendar").
  // 6. If calendarProperty is not undefined, then
  // a. Throw a TypeError exception.
  THROW_IF_NOT_UNDEFINED(temporal_date_like, calendar)
  // 7. Let timeZoneProperty be ? Get(temporalDateLike, "timeZone").
  // 8. If timeZoneProperty is not undefined, then
  // a. Throw a TypeError exception.
  THROW_IF_NOT_UNDEFINED(temporal_date_like, timeZone)
#undef THROW_IF_NOT_UNDEFINED

  // 9. Let calendar be temporalDate.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(temporal_date->calendar(), isolate);

  // 10. Let fieldNames be ? CalendarFields(calendar, « "day", "month",
  // "monthCode", "year" »).
  Handle<FixedArray> field_names = factory->NewFixedArray(4);
  field_names->set(0, *(factory->day_string()));
  field_names->set(1, *(factory->month_string()));
  field_names->set(2, *(factory->monthCode_string()));
  field_names->set(3, *(factory->year_string()));

  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, field_names,
      temporal::CalendarFields(isolate, calendar, field_names),
      JSTemporalPlainDate);
  // 11. Let partialDate be ? PreparePartialTemporalFields(temporalDateLike,
  // fieldNames).
  Handle<JSReceiver> partial_date;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, partial_date,
                             temporal::PreparePartialTemporalFields(
                                 isolate, temporal_date_like, field_names),
                             JSTemporalPlainDate);
  // 12. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainDate);

  // 13. Let fields be ? PrepareTemporalFields(temporalDate, fieldNames, «»).
  Handle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      temporal::PrepareTemporalFields(isolate, temporal_date, field_names,
                                      false, false),
      JSTemporalPlainDate);
  // 14. Set fields to ? CalendarMergeFields(calendar, fields, partialDate).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      temporal::CalendarMergeFields(isolate, calendar, fields, partial_date),
      JSTemporalPlainDate);
  // 15. Set fields to ? PrepareTemporalFields(fields, fieldNames, «»).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             temporal::PrepareTemporalFields(
                                 isolate, fields, field_names, false, false),
                             JSTemporalPlainDate);
  // 16. Return ? DateFromFields(calendar, fields, options).
  return temporal::DateFromFields(isolate, calendar, fields, options);
}

// #sec-temporal.plaindate.prototype.withcalendar
MaybeHandle<JSTemporalPlainDate> JSTemporalPlainDate::WithCalendar(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date,
    Handle<Object> calendar_obj) {
  const char* method = "Temporal.PlainDate.prototype.withCalendar";
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Let calendar be ? ToTemporalCalendar(calendar).
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      temporal::ToTemporalCalendar(isolate, calendar_obj, method),
      JSTemporalPlainDate);
  // 4. Return ? CreateTemporalDate(temporalDate.[[ISOYear]],
  // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]], calendar).
  return temporal::CreateTemporalDate(isolate, temporal_date->iso_year(),
                                      temporal_date->iso_month(),
                                      temporal_date->iso_day(), calendar);
}

namespace {
// #sec-temporal.plaindate.prototype.until
// #sec-temporal.plaindate.prototype.since
MaybeHandle<JSTemporalDuration> UntilOrSince(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date,
    Handle<Object> other_obj, Handle<Object> options_obj, int32_t sign,
    const char* method) {
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Set other to ? ToTemporalDate(other).
  Handle<JSTemporalPlainDate> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, temporal::ToTemporalDate(isolate, other_obj, method),
      JSTemporalDuration);
  // 4. If ? CalendarEquals(temporalDate.[[Calendar]], other.[[Calendar]]) is
  // false, throw a RangeError exception.
  Handle<Oddball> eq;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, eq,
      temporal::CalendarEquals(
          isolate, Handle<JSReceiver>(temporal_date->calendar(), isolate),
          Handle<JSReceiver>(other->calendar(), isolate)),
      JSTemporalDuration);
  if (eq->IsFalse()) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalDuration);
  }
  // 5. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalDuration);
  // 6. Let disallowedUnits be « "hour", "minute", "second", "millisecond",
  // "microsecond", "nanosecond" ».
  std::set<temporal::Unit> disallowed_units(
      {temporal::Unit::kHour, temporal::Unit::kMinute, temporal::Unit::kSecond,
       temporal::Unit::kMillisecond, temporal::Unit::kMicrosecond,
       temporal::Unit::kNanosecond});

  // 7. Let smallestUnit be ? ToSmallestTemporalUnit(options, disallowedUnits,
  // "day").
  Maybe<temporal::Unit> maybe_smallest_unit = temporal::ToSmallestTemporalUnit(
      isolate, options, disallowed_units, temporal::Unit::kDay, method);
  MAYBE_RETURN(maybe_smallest_unit, Handle<JSTemporalDuration>());
  temporal::Unit smallest_unit = maybe_smallest_unit.FromJust();

  // 8. Let largestUnit be ? ToLargestTemporalUnit(options, disallowedUnits,
  // "auto", "day").
  // See https://github.com/tc39/proposal-temporal/issues/1864
  Maybe<temporal::Unit> maybe_largest_unit = temporal::ToLargestTemporalUnit(
      isolate, options, disallowed_units, temporal::Unit::kAuto, smallest_unit,
      method);
  MAYBE_RETURN(maybe_largest_unit, Handle<JSTemporalDuration>());
  temporal::Unit largest_unit = maybe_largest_unit.FromJust();

  // 9. Perform ? ValidateTemporalUnitRange(largestUnit, smallestUnit).
  Maybe<bool> maybe_valid = temporal::ValidateTemporalUnitRange(
      isolate, largest_unit, smallest_unit, method);
  MAYBE_RETURN(maybe_valid, Handle<JSTemporalDuration>());
  CHECK(maybe_valid.FromJust());

  // 10. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(isolate, options,
                                       temporal::RoundingMode::kTrunc, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<JSTemporalDuration>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();

  // 11. Set roundingMode to ! NegateTemporalRoundingMode(roundingMode).
  if (sign == -1) {
    rounding_mode =
        temporal::NegateTemporalRoundingMode(isolate, rounding_mode);
  }

  // 12. Let roundingIncrement be ? ToTemporalRoundingIncrement(options,
  // undefined, false).
  Maybe<int> maybe_rounding_increment = temporal::ToTemporalRoundingIncrement(
      isolate, options, 0, false, false, method);
  MAYBE_RETURN(maybe_rounding_increment, Handle<JSTemporalDuration>());
  int rounding_increment = maybe_rounding_increment.FromJust();

  // 13. Let untilOptions be ? MergeLargestUnitOption(options, largestUnit).
  Handle<JSObject> until_options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, until_options,
      temporal::MergeLargestUnitOption(isolate, options, largest_unit),
      JSTemporalDuration);

  // Temporal.PlainDate.prototype.since

  // 14. Let result be ? CalendarDateUntil(temporalDate.[[Calendar]],
  // other, temporalDate, untilOptions).

  // Temporal.PlainDate.prototype.until

  // 14. Let result be ? CalendarDateUntil(temporalDate.[[Calendar]],
  // temporalDate, other, untilOptions).
  Handle<JSTemporalPlainDate> first = (sign == -1) ? other : temporal_date;
  Handle<JSTemporalPlainDate> second = (sign == -1) ? temporal_date : other;

  Handle<JSTemporalDuration> result;
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(temporal_date->calendar(), isolate);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      temporal::CalendarDateUntil(isolate, calendar, first, second,
                                  until_options),
      JSTemporalDuration);

  // 15. If smallestUnit is "day" and roundingIncrement == 1, then
  if (smallest_unit == temporal::Unit::kDay && rounding_increment == 1) {
    // a. Return ? CreateTemporalDuration(result.[[Years]], result.[[Months]],
    // result.[[Weeks]], result.[[Days]], 0, 0, 0, 0, 0, 0).
    return temporal::CreateTemporalDuration(
        isolate, result->years().Number(), result->months().Number(),
        result->weeks().Number(), result->days().Number(), 0, 0, 0, 0, 0, 0);
  }
  // 16. Let relativeTo be ! CreateTemporalDateTime(temporalDate.[[ISOYear]],
  // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]], 0, 0, 0, 0, 0, 0,
  // temporalDate.[[Calendar]]).
  Handle<JSTemporalPlainDateTime> relative_to;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, relative_to,
      temporal::CreateTemporalDateTime(
          isolate, temporal_date->iso_year(), temporal_date->iso_month(),
          temporal_date->iso_day(), 0, 0, 0, 0, 0, 0, calendar),
      JSTemporalDuration);
  // Temporal.PlainDate.prototype.since

  // 17. Set result to ? RoundDuration(-result.[[Years]], -result.[[Months]],
  // -result.[[Weeks]], -result.[[Days]], 0, 0, 0, 0, 0, 0, roundingIncrement,
  // smallestUnit, roundingMode, relativeTo).

  // Temporal.PlainDate.prototype.until

  // 17. Set result to ? RoundDuration(result.[[Years]], result.[[Months]],
  // result.[[Weeks]], result.[[Days]], 0, 0, 0, 0, 0, 0, roundingIncrement,
  // smallestUnit, roundingMode, relativeTo).
  double remainder;
  Maybe<temporal::DurationRecord> maybe_round_result = temporal::RoundDuration(
      isolate,
      {sign * result->years().Number(), sign * result->months().Number(),
       sign * result->weeks().Number(), sign * result->days().Number(), 0, 0, 0,
       0, 0, 0},
      rounding_increment, smallest_unit, rounding_mode, relative_to, &remainder,
      method);
  MAYBE_RETURN(maybe_round_result, Handle<JSTemporalDuration>());
  temporal::DurationRecord round_result = maybe_round_result.FromJust();
  // Temporal.PlainDate.prototype.since

  // 18. Return ? CreateTemporalDuration(-result.[[Years]], -result.[[Months]],
  // -result.[[Weeks]], -result.[[Days]], 0, 0, 0, 0, 0, 0).

  // Temporal.PlainDate.prototype.until

  // 18. Return ? CreateTemporalDuration(result.[[Years]], result.[[Months]],
  // result.[[Weeks]], result.[[Days]], 0, 0, 0, 0, 0, 0).
  return temporal::CreateTemporalDuration(
      isolate, sign * round_result.years, sign * round_result.months,
      sign * round_result.weeks, sign * round_result.days, 0, 0, 0, 0, 0, 0);
}

}  // namespace

// #sec-temporal.plaindate.prototype.until
MaybeHandle<JSTemporalDuration> JSTemporalPlainDate::Until(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date,
    Handle<Object> other_obj, Handle<Object> options_obj) {
  return UntilOrSince(isolate, temporal_date, other_obj, options_obj, 1,
                      "Temporal.PlainDate.prototype.until");
}

// #sec-temporal.plaindate.prototype.since
MaybeHandle<JSTemporalDuration> JSTemporalPlainDate::Since(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date,
    Handle<Object> other_obj, Handle<Object> options_obj) {
  return UntilOrSince(isolate, temporal_date, other_obj, options_obj, -1,
                      "Temporal.PlainDate.prototype.since");
}

// #sec-temporal.plaindate.prototype.equals
MaybeHandle<Oddball> JSTemporalPlainDate::Equals(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date,
    Handle<Object> other_obj) {
  const char* method = "Temporal.PlainDate.prototype.equals";
  Factory* factory = isolate->factory();
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Set other to ? ToTemporalDate(other).
  Handle<JSTemporalPlainDate> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, temporal::ToTemporalDate(isolate, other_obj, method),
      Oddball);
  // 4. If temporalDate.[[ISOYear]] ≠ other.[[ISOYear]], return false.
  if (temporal_date->iso_year() != other->iso_year())
    return factory->false_value();
  // 5. If temporalDate.[[ISOMonth]] ≠ other.[[ISOMonth]], return false.
  if (temporal_date->iso_month() != other->iso_month())
    return factory->false_value();
  // 6. If temporalDate.[[ISODay]] ≠ other.[[ISODay]], return false.
  if (temporal_date->iso_day() != other->iso_day())
    return factory->false_value();
  // 7. Return ? CalendarEquals(temporalDate.[[Calendar]], other.[[Calendar]]).
  return temporal::CalendarEquals(
      isolate, Handle<JSReceiver>(temporal_date->calendar(), isolate),
      Handle<JSReceiver>(other->calendar(), isolate));
}

// #sec-temporal.plaindate.prototype.tozoneddatetime
MaybeHandle<JSTemporalZonedDateTime> JSTemporalPlainDate::ToZonedDateTime(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date,
    Handle<Object> item_obj) {
  const char* method = "Temporal.PlainDate.prototype.toZonedDateTime";
  Factory* factory = isolate->factory();
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. If Type(item) is Object, then
  Handle<JSReceiver> time_zone;
  Handle<Object> temporal_time_obj;
  if (item_obj->IsJSReceiver()) {
    Handle<JSReceiver> item = Handle<JSReceiver>::cast(item_obj);
    // a. Let timeZoneLike be ? Get(item, "timeZone").
    Handle<Object> time_zone_like;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone_like,
        JSReceiver::GetProperty(isolate, item, factory->timeZone_string()),
        JSTemporalZonedDateTime);
    // b. If timeZoneLike is undefined, then
    if (time_zone_like->IsUndefined()) {
      // i. Let timeZone be ? ToTemporalTimeZone(item).
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, time_zone,
          temporal::ToTemporalTimeZone(isolate, item, method),
          JSTemporalZonedDateTime);
      // ii. Let temporalTime be undefined.
      temporal_time_obj = factory->undefined_value();
      // c. Else,
    } else {
      // i. Let timeZone be ? ToTemporalTimeZone(timeZoneLike).
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, time_zone,
          temporal::ToTemporalTimeZone(isolate, time_zone_like, method),
          JSTemporalZonedDateTime);
      // ii. Let temporalTime be ? Get(item, "plainTime").
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, temporal_time_obj,
          JSReceiver::GetProperty(isolate, item, factory->plainTime_string()),
          JSTemporalZonedDateTime);
    }
    // 4. Else,
  } else {
    // a. Let timeZone be ? ToTemporalTimeZone(item).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone,
        temporal::ToTemporalTimeZone(isolate, item_obj, method),
        JSTemporalZonedDateTime);
    // b. Let temporalTime be undefined.
    temporal_time_obj = factory->undefined_value();
  }
  // 5. If temporalTime is undefined, then
  Handle<JSTemporalPlainDateTime> temporal_date_time;
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(temporal_date->calendar(), isolate);
  if (temporal_time_obj->IsUndefined()) {
    // a. Let temporalDateTime be ?
    // CreateTemporalDateTime(temporalDate.[[ISOYear]],
    // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]], 0, 0, 0, 0, 0, 0,
    // temporalDate.[[Calendar]]).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_time,
        temporal::CreateTemporalDateTime(
            isolate, temporal_date->iso_year(), temporal_date->iso_month(),
            temporal_date->iso_day(), 0, 0, 0, 0, 0, 0, calendar),
        JSTemporalZonedDateTime);
    // 6. Else,
  } else {
    Handle<JSTemporalPlainTime> temporal_time;
    // a. Set temporalTime to ? ToTemporalTime(temporalTime).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_time,
        temporal::ToTemporalTime(isolate, temporal_time_obj, method),
        JSTemporalZonedDateTime);
    // b. Let temporalDateTime be ?
    // CreateTemporalDateTime(temporalDate.[[ISOYear]],
    // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]],
    // temporalTime.[[ISOHour]], temporalTime.[[ISOMinute]],
    // temporalTime.[[ISOSecond]], temporalTime.[[ISOMillisecond]],
    // temporalTime.[[ISOMicrosecond]], temporalTime.[[ISONanosecond]],
    // temporalDate.[[Calendar]]).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, temporal_date_time,
        temporal::CreateTemporalDateTime(
            isolate, temporal_date->iso_year(), temporal_date->iso_month(),
            temporal_date->iso_day(), temporal_time->iso_hour(),
            temporal_time->iso_minute(), temporal_time->iso_second(),
            temporal_time->iso_millisecond(), temporal_time->iso_microsecond(),
            temporal_time->iso_nanosecond(), calendar),
        JSTemporalZonedDateTime);
  }
  // 7. Let instant be ? BuiltinTimeZoneGetInstantFor(timeZone,
  // temporalDateTime, "compatible").
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, temporal_date_time,
                                   temporal::Disambiguation::kCompatible),
      JSTemporalZonedDateTime);
  // 8. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // temporalDate.[[Calendar]]).
  return temporal::CreateTemporalZonedDateTime(
      isolate, Handle<BigInt>(instant->nanoseconds(), isolate), time_zone,
      calendar);
}

// #sec-temporal.plaindate.prototype.tostring
MaybeHandle<String> JSTemporalPlainDate::ToString(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date,
    Handle<Object> options_obj) {
  const char* method = "Temporal.PlainDate.prototype.toString";
  // 1. Let temporalDate be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDate,
  // [[InitializedTemporalDate]]).
  // 3. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method), String);
  // 4. Let showCalendar be ? ToShowCalendarOption(options).
  Maybe<temporal::ShowCalendar> maybe_show_calendar =
      temporal::ToShowCalendarOption(isolate, options, method);
  MAYBE_RETURN(maybe_show_calendar, Handle<String>());
  temporal::ShowCalendar show_calendar = maybe_show_calendar.FromJust();

  // 5. Return ? TemporalDateToString(temporalDate, showCalendar).
  return temporal::TemporalDateToString(
      isolate, temporal_date->iso_year(), temporal_date->iso_month(),
      temporal_date->iso_day(),
      Handle<JSReceiver>(temporal_date->calendar(), isolate), show_calendar);
}

// #sec-temporal.plaindate.prototype.tolocalestring
MaybeHandle<String> JSTemporalPlainDate::ToLocaleString(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date,
    Handle<Object> locales, Handle<Object> options) {
  return temporal::TemporalDateToString(
      isolate, temporal_date->iso_year(), temporal_date->iso_month(),
      temporal_date->iso_day(),
      Handle<JSReceiver>(temporal_date->calendar(), isolate),
      temporal::ShowCalendar::kAuto);
}

// #sec-temporal.plaindate.prototype.tojson
MaybeHandle<String> JSTemporalPlainDate::ToJSON(
    Isolate* isolate, Handle<JSTemporalPlainDate> temporal_date) {
  return TemporalDateToString(
      isolate, temporal_date->iso_year(), temporal_date->iso_month(),
      temporal_date->iso_day(),
      Handle<JSReceiver>(temporal_date->calendar(), isolate),
      temporal::ShowCalendar::kAuto);
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

// #sec-temporal.now.plaindatetime
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Now(
    Isolate* isolate, Handle<Object> calendar,
    Handle<Object> temporal_time_zone_like) {
  const char* method = "Temporal.Now.plainDateTime";
  // 1. Return ? SystemDateTime(temporalTimeZoneLike, calendar).
  return temporal::SystemDateTime(isolate, temporal_time_zone_like, calendar,
                                  method);
}

// #sec-temporal.now.plaindatetimeiso
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::NowISO(
    Isolate* isolate, Handle<Object> temporal_time_zone_like) {
  const char* method = "Temporal.Now.plainDateTimeISO";
  // 1. Let calendar be ! GetISO8601Calendar().
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             temporal::GetISO8601Calendar(isolate),
                             JSTemporalPlainDateTime);
  // 2. Return ? SystemDateTime(temporalTimeZoneLike, calendar).
  return temporal::SystemDateTime(isolate, temporal_time_zone_like, calendar,
                                  method);
}

// #sec-temporal.plaindatetime.from
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::From(
    Isolate* isolate, Handle<Object> item, Handle<Object> options_obj) {
  const char* method = "Temporal.PlainDateTime.from";
  // 1. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainDateTime);
  // 2. If Type(item) is Object and item has an [[InitializedTemporalDateTime]]
  // internal slot, then
  if (item->IsJSTemporalPlainDateTime()) {
    // a. Perform ? ToTemporalOverflow(options).
    Maybe<temporal::ShowOverflow> maybe_overflow =
        temporal::ToTemporalOverflow(isolate, options, method);
    MAYBE_RETURN(maybe_overflow, Handle<JSTemporalPlainDateTime>());
    // b. Return ? CreateTemporalDateTime(item.[[ISYear]], item.[[ISOMonth]],
    // item.[[ISODay]], item.[[ISOHour]], item.[[ISOMinute]],
    // item.[[ISOSecond]], item.[[ISOMillisecond]], item.[[ISOMicrosecond]],
    // item.[[ISONanosecond]], item.[[Calendar]]).
    Handle<JSTemporalPlainDateTime> date_time =
        Handle<JSTemporalPlainDateTime>::cast(item);
    return temporal::CreateTemporalDateTime(
        isolate, date_time->iso_year(), date_time->iso_month(),
        date_time->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
        date_time->iso_second(), date_time->iso_millisecond(),
        date_time->iso_microsecond(), date_time->iso_nanosecond(),
        Handle<JSReceiver>(date_time->calendar(), isolate));
  }
  // 3. Return ? ToTemporalDateTime(item, options).
  return temporal::ToTemporalDateTime(isolate, item, options, method);
}

// #sec-temporal.plaindatetime.compare
MaybeHandle<Smi> JSTemporalPlainDateTime::Compare(Isolate* isolate,
                                                  Handle<Object> one_obj,
                                                  Handle<Object> two_obj) {
  const char* method = "Temporal.PlainDateTime.compare";
  // 1. Set one to ? ToTemporalDateTime(one).
  Handle<JSTemporalPlainDateTime> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one, temporal::ToTemporalDateTime(isolate, one_obj, method),
      Smi);
  // 2. Set two to ? ToTemporalDateTime(two).
  Handle<JSTemporalPlainDateTime> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two, temporal::ToTemporalDateTime(isolate, two_obj, method),
      Smi);
  // 3. Return 𝔽(! CompareISODateTime(one.[[ISOYear]], one.[[ISOMonth]],
  // one.[[ISODay]], one.[[ISOHour]], one.[[ISOMinute]], one.[[ISOSecond]],
  // one.[[ISOMillisecond]], one.[[ISOMicrosecond]], one.[[ISONanosecond]],
  // two.[[ISOYear]], two.[[ISOMonth]], two.[[ISODay]], two.[[ISOHour]],
  // two.[[ISOMinute]], two.[[ISOSecond]], two.[[ISOMillisecond]],
  // two.[[ISOMicrosecond]], two.[[ISONanosecond]])).
  return Handle<Smi>(
      Smi::FromInt(temporal::CompareISODateTime(
          isolate, one->iso_year(), one->iso_month(), one->iso_day(),
          one->iso_hour(), one->iso_minute(), one->iso_second(),
          one->iso_millisecond(), one->iso_microsecond(), one->iso_nanosecond(),
          two->iso_year(), two->iso_month(), two->iso_day(), two->iso_hour(),
          two->iso_minute(), two->iso_second(), two->iso_millisecond(),
          two->iso_microsecond(), two->iso_nanosecond())),
      isolate);
}

// #sec-temporal.plaindatetime.prototype.tostring
MaybeHandle<String> JSTemporalPlainDateTime::ToString(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> options_obj) {
  const char* method = "Temporal.PlainDateTime.prototype.toString";
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method), String);
  // 4. Let precision be ? ToSecondsStringPrecision(options).
  temporal::Precision precision;
  double increment;
  temporal::Unit unit;
  Maybe<bool> maybe_precision = temporal::ToSecondsStringPrecision(
      isolate, options, &precision, &increment, &unit, method);
  MAYBE_RETURN(maybe_precision, Handle<String>());
  CHECK(maybe_precision.FromJust());

  // 5. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(isolate, options,
                                       temporal::RoundingMode::kTrunc, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<String>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();

  // 6. Let showCalendar be ? ToShowCalendarOption(options).
  Maybe<temporal::ShowCalendar> maybe_show_calendar =
      temporal::ToShowCalendarOption(isolate, options, method);
  MAYBE_RETURN(maybe_show_calendar, Handle<String>());
  temporal::ShowCalendar show_calendar = maybe_show_calendar.FromJust();

  // 7. Let result be ! RoundISODateTime(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]], precision.[[Increment]], precision.[[Unit]],
  // roundingMode).
  temporal::DateTimeRecordCommon result = temporal::RoundISODateTime(
      isolate, date_time->iso_year(), date_time->iso_month(),
      date_time->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
      date_time->iso_second(), date_time->iso_millisecond(),
      date_time->iso_microsecond(), date_time->iso_nanosecond(), increment,
      unit, rounding_mode);
  // 8. Return ? TemporalDateTimeToString(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // dateTime.[[Calendar]], precision.[[Precision]], showCalendar).

  return temporal::TemporalDateTimeToString(
      isolate, result.year, result.month, result.day, result.hour,
      result.minute, result.second, result.millisecond, result.microsecond,
      result.nanosecond, Handle<JSReceiver>(date_time->calendar(), isolate),
      precision, show_calendar);
}

// #sec-temporal.plaindatetime.prototype.tolocalestring
MaybeHandle<String> JSTemporalPlainDateTime::ToLocaleString(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> locales, Handle<Object> options) {
  return temporal::TemporalDateTimeToString(
      isolate, date_time->iso_year(), date_time->iso_month(),
      date_time->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
      date_time->iso_second(), date_time->iso_millisecond(),
      date_time->iso_microsecond(), date_time->iso_nanosecond(),
      Handle<JSReceiver>(date_time->calendar(), isolate),
      temporal::Precision::kAuto, temporal::ShowCalendar::kAuto);
}

// #sec-temporal.plaindatetime.prototype.tojson
MaybeHandle<String> JSTemporalPlainDateTime::ToJSON(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time) {
  return temporal::TemporalDateTimeToString(
      isolate, date_time->iso_year(), date_time->iso_month(),
      date_time->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
      date_time->iso_second(), date_time->iso_millisecond(),
      date_time->iso_microsecond(), date_time->iso_nanosecond(),
      Handle<JSReceiver>(date_time->calendar(), isolate),
      temporal::Precision::kAuto, temporal::ShowCalendar::kAuto);
}

// #sec-temporal.plaindatetime.prototype.with
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::With(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> temporal_date_time_like_obj, Handle<Object> options_obj) {
  const char* method = "Temporal.PlainDateTime.prototype.with";
  Factory* factory = isolate->factory();
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. If Type(temporalDateTimeLike) is not Object, then
  if (!temporal_date_time_like_obj->IsJSReceiver()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainDateTime);
  }
  Handle<JSReceiver> temporal_date_time_like =
      Handle<JSReceiver>::cast(temporal_date_time_like_obj);
  // 4. Perform ? RejectTemporalCalendarType(temporalDateTimeLike).
  Maybe<bool> maybe_reject_temporal_calendar_type =
      temporal::RejectTemporalCalendarType(isolate, temporal_date_time_like);
  MAYBE_RETURN(maybe_reject_temporal_calendar_type,
               Handle<JSTemporalPlainDateTime>());
  // 5. Let calendarProperty be ? Get(temporalDateTimeLike, "calendar").
  Handle<Object> calendar_property;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_property,
      Object::GetPropertyOrElement(isolate, temporal_date_time_like,
                                   factory->calendar_string()),
      JSTemporalPlainDateTime);

  // 6. If calendarProperty is not undefined, then
  if (!calendar_property->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainDateTime);
  }
  // 7. Let timeZoneProperty be ? Get(temporalDateTimeLike, "timeZone").
  Handle<Object> time_zone_property;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone_property,
      Object::GetPropertyOrElement(isolate, temporal_date_time_like,
                                   factory->timeZone_string()),
      JSTemporalPlainDateTime);
  // 8. If timeZoneProperty is not undefined, then
  if (!time_zone_property->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainDateTime);
  }
  // 9. Let calendar be dateTime.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(date_time->calendar(), isolate);
  // 10. Let fieldNames be ? CalendarFields(calendar, « "day", "hour",
  // "microsecond", "millisecond", "minute", "month", "monthCode",
  // "nanosecond", "second", "year" »).
  Handle<FixedArray> field_names = factory->NewFixedArray(10);
  int i = 0;
  field_names->set(i++, *(factory->day_string()));
  field_names->set(i++, *(factory->hour_string()));
  field_names->set(i++, *(factory->microsecond_string()));
  field_names->set(i++, *(factory->millisecond_string()));
  field_names->set(i++, *(factory->minute_string()));
  field_names->set(i++, *(factory->month_string()));
  field_names->set(i++, *(factory->monthCode_string()));
  field_names->set(i++, *(factory->nanosecond_string()));
  field_names->set(i++, *(factory->second_string()));
  field_names->set(i++, *(factory->year_string()));
  CHECK_EQ(i, 10);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, field_names,
      temporal::CalendarFields(isolate, calendar, field_names),
      JSTemporalPlainDateTime);

  // 11. Let partialDateTime be ?
  // PreparePartialTemporalFields(temporalDateTimeLike, fieldNames).
  Handle<JSReceiver> partial_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, partial_date_time,
                             temporal::PreparePartialTemporalFields(
                                 isolate, temporal_date_time_like, field_names),
                             JSTemporalPlainDateTime);

  // 12. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainDateTime);
  // 13. Let fields be ? PrepareTemporalFields(dateTime, fieldNames, «»).
  Handle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             temporal::PrepareTemporalFields(
                                 isolate, date_time, field_names, false, false),
                             JSTemporalPlainDateTime);

  // 14. Set fields to ? CalendarMergeFields(calendar, fields, partialDateTime).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             temporal::CalendarMergeFields(
                                 isolate, calendar, fields, partial_date_time),
                             JSTemporalPlainDateTime);
  // 15. Set fields to ? PrepareTemporalFields(fields, fieldNames, «»).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             temporal::PrepareTemporalFields(
                                 isolate, fields, field_names, false, false),
                             JSTemporalPlainDateTime);
  // 16. Let result be ? InterpretTemporalDateTimeFields(calendar, fields,
  // options).
  Maybe<temporal::DateTimeRecord> maybe_result =
      temporal::InterpretTemporalDateTimeFields(isolate, calendar, fields,
                                                options, method);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalPlainDateTime>());
  temporal::DateTimeRecord result = maybe_result.FromJust();
  // 17. Assert: ! IsValidISODate(result.[[Year]], result.[[Month]],
  // result.[[Day]]) is true.
  CHECK(
      temporal::IsValidISODate(isolate, result.year, result.month, result.day));
  // 18. Assert: ! IsValidTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]) is true.
  CHECK(temporal::IsValidTime(isolate, result.hour, result.minute,
                              result.second, result.millisecond,
                              result.microsecond, result.nanosecond));
  // 19. Return ? CreateTemporalDateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // calendar).
  return temporal::CreateTemporalDateTime(
      isolate, result.year, result.month, result.day, result.hour,
      result.minute, result.second, result.millisecond, result.microsecond,
      result.nanosecond, calendar);
}

// #sec-temporal.plaindatetime.prototype.withplaintime
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::WithPlainTime(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> plain_time_like) {
  const char* method = "Temporal.PlainDateTime.prototype.withPlainTime";
  // 1. Let temporalDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. If plainTimeLike is undefined, then
  if (plain_time_like->IsUndefined()) {
    // a. Return ? CreateTemporalDateTime(temporalDateTime.[[ISOYear]],
    // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]], 0, 0, 0, 0,
    // 0, 0, temporalDateTime.[[Calendar]]).
    return temporal::CreateTemporalDateTime(
        isolate, date_time->iso_year(), date_time->iso_month(),
        date_time->iso_day(), 0, 0, 0, 0, 0, 0,
        Handle<JSReceiver>(date_time->calendar(), isolate));
  }
  Handle<JSTemporalPlainTime> plain_time;
  // 4. Let plainTime be ? ToTemporalTime(plainTimeLike).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, plain_time,
      temporal::ToTemporalTime(isolate, plain_time_like,
                               temporal::ShowOverflow::kConstrain, method),
      JSTemporalPlainDateTime);
  // 5. Return ? CreateTemporalDateTime(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]],
  // plainTime.[[ISOHour]], plainTime.[[ISOMinute]], plainTime.[[ISOSecond]],
  // plainTime.[[ISOMillisecond]], plainTime.[[ISOMicrosecond]],
  // plainTime.[[ISONanosecond]], temporalDateTime.[[Calendar]]).
  return temporal::CreateTemporalDateTime(
      isolate, date_time->iso_year(), date_time->iso_month(),
      date_time->iso_day(), plain_time->iso_hour(), plain_time->iso_minute(),
      plain_time->iso_second(), plain_time->iso_millisecond(),
      plain_time->iso_microsecond(), plain_time->iso_nanosecond(),
      Handle<JSReceiver>(date_time->calendar(), isolate));
}

// #sec-temporal.plaindatetime.prototype.withplaindate
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::WithPlainDate(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> temporal_date_like) {
  const char* method = "Temporal.PlainDateTime.prototype.withPlainDate";
  // 1. Let temporalDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Let plainDate be ? ToTemporalDate(plainDateLike).
  Handle<JSTemporalPlainDate> plain_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, plain_date,
      temporal::ToTemporalDate(isolate, temporal_date_like, method),
      JSTemporalPlainDateTime);
  // 4. Let calendar be ? ConsolidateCalendars(temporalDateTime.[[Calendar]],
  // plainDate.[[Calendar]]).
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      temporal::ConsolidateCalendars(
          isolate, Handle<JSReceiver>(date_time->calendar(), isolate),
          Handle<JSReceiver>(plain_date->calendar(), isolate)),
      JSTemporalPlainDateTime);
  // 5. Return ? CreateTemporalDateTime(plainDate.[[ISOYear]],
  // plainDate.[[ISOMonth]], plainDate.[[ISODay]], temporalDateTime.[[ISOHour]],
  // temporalDateTime.[[ISOMinute]], temporalDateTime.[[ISOSecond]],
  // temporalDateTime.[[ISOMillisecond]], temporalDateTime.[[ISOMicrosecond]],
  // temporalDateTime.[[ISONanosecond]], calendar).
  return temporal::CreateTemporalDateTime(
      isolate, plain_date->iso_year(), plain_date->iso_month(),
      plain_date->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
      date_time->iso_second(), date_time->iso_millisecond(),
      date_time->iso_microsecond(), date_time->iso_nanosecond(), calendar);
}

// #sec-temporal.plaindatetime.prototype.withcalendar
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::WithCalendar(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> calendar_obj) {
  const char* method = "Temporal.PlainDateTime.prototype.withCalendar";
  // 1. Let temporalDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalDateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Let calendar be ? ToTemporalCalendar(calendar).
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      temporal::ToTemporalCalendar(isolate, calendar_obj, method),
      JSTemporalPlainDateTime);
  // 4. Return ? CreateTemporalDateTime(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]],
  // temporalDateTime.[[ISOHour]], temporalDateTime.[[ISOMinute]],
  // temporalDateTime.[[ISOSecond]], temporalDateTime.[[ISOMillisecond]],
  // temporalDateTime.[[ISOMicrosecond]], temporalDateTime.[[ISONanosecond]],
  // calendar).
  return temporal::CreateTemporalDateTime(
      isolate, date_time->iso_year(), date_time->iso_month(),
      date_time->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
      date_time->iso_second(), date_time->iso_millisecond(),
      date_time->iso_microsecond(), date_time->iso_nanosecond(), calendar);
}

namespace {
// #sec-temporal.plaindatetime.prototype.add
// #sec-temporal.plaindatetime.prototype.subtract
MaybeHandle<JSTemporalPlainDateTime> AddOrSubtract(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> temporal_duration_like, Handle<Object> options_obj,
    int factor, const char* method) {
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Let duration be ? ToLimitedTemporalDuration(temporalDurationLike, « »).
  Maybe<temporal::DurationRecord> maybe_duration = ToLimitedTemporalDuration(
      isolate, temporal_duration_like, std::set<temporal::Unit>({}), method);
  MAYBE_RETURN(maybe_duration, Handle<JSTemporalPlainDateTime>());
  temporal::DurationRecord duration = maybe_duration.FromJust();
  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainDateTime);
  // 5. Let result be ? AddDateTime(dateTime.[[ISOYear]], dateTime.[[ISOMonth]],
  // dateTime.[[ISODay]], dateTime.[[ISOHour]], dateTime.[[ISOMinute]],
  // dateTime.[[ISOSecond]], dateTime.[[ISOMillisecond]],
  // dateTime.[[ISOMicrosecond]], dateTime.[[ISONanosecond]],
  // dateTime.[[Calendar]], duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], duration.[[Days]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]], options).
  Maybe<temporal::DateTimeRecordCommon> maybe_result = temporal::AddDateTime(
      isolate, date_time->iso_year(), date_time->iso_month(),
      date_time->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
      date_time->iso_second(), date_time->iso_millisecond(),
      date_time->iso_microsecond(), date_time->iso_nanosecond(),
      Handle<JSReceiver>(date_time->calendar(), isolate),
      {factor * duration.years, factor * duration.months,
       factor * duration.weeks, factor * duration.days, factor * duration.hours,
       factor * duration.minutes, factor * duration.seconds,
       factor * duration.milliseconds, factor * duration.microseconds,
       factor * duration.nanoseconds},
      options);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalPlainDateTime>());
  temporal::DateTimeRecordCommon result = maybe_result.FromJust();
  // 6. Assert: ! IsValidISODate(result.[[Year]], result.[[Month]],
  // result.[[Day]]) is true.
  CHECK(
      temporal::IsValidISODate(isolate, result.year, result.month, result.day));
  // 7. Assert: ! IsValidTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]) is true.
  CHECK(temporal::IsValidTime(isolate, result.hour, result.minute,
                              result.second, result.millisecond,
                              result.microsecond, result.nanosecond));
  // 8. Return ? CreateTemporalDateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // dateTime.[[Calendar]]).
  return temporal::CreateTemporalDateTime(
      isolate, result.year, result.month, result.day, result.hour,
      result.minute, result.second, result.millisecond, result.microsecond,
      result.nanosecond, Handle<JSReceiver>(date_time->calendar(), isolate));
}

}  // namespace
// #sec-temporal.plaindatetime.prototype.subtract
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Add(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> temporal_duration_like, Handle<Object> options) {
  return AddOrSubtract(isolate, date_time, temporal_duration_like, options, 1,
                       "Temporal.PlainDateTime.prototype.add");
}

// #sec-temporal.plaindatetime.prototype.subtract
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Subtract(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> temporal_duration_like, Handle<Object> options) {
  return AddOrSubtract(isolate, date_time, temporal_duration_like, options, -1,
                       "Temporal.PlainDateTime.prototype.subtract");
}

namespace {

// #sec-temporal.plaindatetime.prototype.until
// #sec-temporal.plaindatetime.prototype.since
MaybeHandle<JSTemporalDuration> UntilOrSince(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> other_obj, Handle<Object> options_obj, int32_t sign,
    const char* method) {
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Set other to ? ToTemporalDateTime(other).
  Handle<JSTemporalPlainDateTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, temporal::ToTemporalDateTime(isolate, other_obj, method),
      JSTemporalDuration);

  // 4. If ? CalendarEquals(dateTime.[[Calendar]], other.[[Calendar]]) is false,
  // throw a RangeError exception.
  Handle<Oddball> eq;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, eq,
      temporal::CalendarEquals(
          isolate, Handle<JSReceiver>(date_time->calendar(), isolate),
          Handle<JSReceiver>(other->calendar(), isolate)),
      JSTemporalDuration);
  if (eq->IsFalse()) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalDuration);
  }

  // 5. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalDuration);

  // 6. Let smallestUnit be ? ToSmallestTemporalUnit(options, « »,
  // "nanosecond").
  Maybe<temporal::Unit> maybe_smallest_unit =
      ToSmallestTemporalUnit(isolate, options, std::set<temporal::Unit>({}),
                             temporal::Unit::kNanosecond, method);
  MAYBE_RETURN(maybe_smallest_unit, Handle<JSTemporalDuration>());
  temporal::Unit smallest_unit = maybe_smallest_unit.FromJust();

  // 7. Let defaultLargestUnit be ! LargerOfTwoTemporalUnits("day",
  // smallestUnit).
  temporal::Unit default_largest_unit = temporal::LargerOfTwoTemporalUnits(
      isolate, temporal::Unit::kDay, smallest_unit);

  // 8. Let largestUnit be ? ToLargestTemporalUnit(options, « », "auto",
  // defaultLargestUnit).
  Maybe<temporal::Unit> maybe_largest_unit = temporal::ToLargestTemporalUnit(
      isolate, options, std::set<temporal::Unit>({}), temporal::Unit::kAuto,
      default_largest_unit, method);
  MAYBE_RETURN(maybe_largest_unit, Handle<JSTemporalDuration>());
  temporal::Unit largest_unit = maybe_largest_unit.FromJust();

  // 9. Perform ? ValidateTemporalUnitRange(largestUnit, smallestUnit).
  Maybe<bool> maybe_valid = temporal::ValidateTemporalUnitRange(
      isolate, largest_unit, smallest_unit, method);
  MAYBE_RETURN(maybe_valid, Handle<JSTemporalDuration>());
  CHECK(maybe_valid.FromJust());

  // 10. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(isolate, options,
                                       temporal::RoundingMode::kTrunc, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<JSTemporalDuration>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();

  // 11. Set roundingMode to ! NegateTemporalRoundingMode(roundingMode).
  if (sign == -1) {
    rounding_mode = NegateTemporalRoundingMode(isolate, rounding_mode);
  }

  // 12. Let maximum be !
  // MaximumTemporalDurationRoundingIncrement(smallestUnit).
  double maximum = 0;
  Maybe<bool> maybe_maximum = MaximumTemporalDurationRoundingIncrement(
      isolate, smallest_unit, &maximum);
  MAYBE_RETURN(maybe_maximum, Handle<JSTemporalDuration>());
  bool maximum_is_defined = maybe_maximum.FromJust();

  // 13. Let roundingIncrement be ? ToTemporalRoundingIncrement(options,
  // maximum, false).
  Maybe<int> maybe_rounding_increment = temporal::ToTemporalRoundingIncrement(
      isolate, options, maximum, maximum_is_defined, false, method);
  MAYBE_RETURN(maybe_rounding_increment, Handle<JSTemporalDuration>());
  int rounding_increment = maybe_rounding_increment.FromJust();

  Handle<JSTemporalPlainDateTime> first = (sign < 0) ? other : date_time;
  Handle<JSTemporalPlainDateTime> second = (sign < 0) ? date_time : other;
  // Temporal.PlainDateTime.prototype.since

  // 14. Let diff be ? DifferenceISODateTime(other.[[ISOYear]],
  // other.[[ISOMonth]], other.[[ISODay]], other.[[ISOHour]],
  // other.[[ISOMinute]], other.[[ISOSecond]], other.[[ISOMillisecond]],
  // other.[[ISOMicrosecond]], other.[[ISONanosecond]], dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]], dateTime.[[Calendar]], largestUnit, options).

  // Temporal.PlainDateTime.prototype.until

  // 13. Let diff be ? DifferenceISODateTime(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]], other.[[ISOYear]], other.[[ISOMonth]],
  // other.[[ISODay]], other.[[ISOHour]], other.[[ISOMinute]],
  // other.[[ISOSecond]], other.[[ISOMillisecond]], other.[[ISOMicrosecond]],
  // other.[[ISONanosecond]], dateTime.[[Calendar]], largestUnit, options).
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(date_time->calendar(), isolate);

  Maybe<temporal::DurationRecord> maybe_diff = DifferenceISODateTime(
      isolate, first->iso_year(), first->iso_month(), first->iso_day(),
      first->iso_hour(), first->iso_minute(), first->iso_second(),
      first->iso_millisecond(), first->iso_microsecond(),
      first->iso_nanosecond(), second->iso_year(), second->iso_month(),
      second->iso_day(), second->iso_hour(), second->iso_minute(),
      second->iso_second(), second->iso_millisecond(),
      second->iso_microsecond(), second->iso_nanosecond(), calendar,
      largest_unit, options_obj);

  MAYBE_RETURN(maybe_diff, Handle<JSTemporalDuration>());
  temporal::DurationRecord diff = maybe_diff.FromJust();

  // Temporal.PlainDateTime.prototype.since

  // 15. Let roundResult be ? RoundDuration(−diff.[[Years]], −diff.[[Months]],
  // −diff.[[Weeks]], −diff.[[Days]], −diff.[[Hours]], −diff.[[Minutes]],
  // −diff.[[Seconds]], −diff.[[Milliseconds]], −diff.[[Microseconds]],
  // −diff.[[Nanoseconds]], roundingIncrement, smallestUnit, roundingMode,
  // dateTime).
  diff.years *= sign;
  diff.months *= sign;
  diff.weeks *= sign;
  diff.days *= sign;
  diff.hours *= sign;
  diff.minutes *= sign;
  diff.seconds *= sign;
  diff.milliseconds *= sign;
  diff.microseconds *= sign;
  diff.nanoseconds *= sign;

  // Temporal.PlainDateTime.prototype.until

  // 14. Let roundResult be ? RoundDuration(diff.[[Years]], diff.[[Months]],
  // diff.[[Weeks]], diff.[[Days]], diff.[[Hours]], diff.[[Minutes]],
  // diff.[[Seconds]], diff.[[Milliseconds]], diff.[[Microseconds]],
  // diff.[[Nanoseconds]], roundingIncrement, smallestUnit, roundingMode,
  // dateTime).
  double remainder;
  Maybe<temporal::DurationRecord> maybe_round_result =
      RoundDuration(isolate, diff, rounding_increment, smallest_unit,
                    rounding_mode, date_time, &remainder, method);
  MAYBE_RETURN(maybe_round_result, Handle<JSTemporalDuration>());
  temporal::DurationRecord round_result = maybe_round_result.FromJust();

  // 16. Let result be ! BalanceDuration(−roundResult.[[Days]],
  // −roundResult.[[Hours]], −roundResult.[[Minutes]], −roundResult.[[Seconds]],
  // −roundResult.[[Milliseconds]], −roundResult.[[Microseconds]],
  // −roundResult.[[Nanoseconds]], largestUnit).
  double result_days = round_result.days * sign;
  double result_hours = round_result.hours * sign;
  double result_minutes = round_result.minutes * sign;
  double result_seconds = round_result.seconds * sign;
  double result_milliseconds = round_result.milliseconds * sign;
  double result_microseconds = round_result.microseconds * sign;
  double result_nanoseconds = round_result.nanoseconds * sign;

  Maybe<bool> maybe_result =
      BalanceDuration(isolate, &result_days, &result_hours, &result_minutes,
                      &result_seconds, &result_milliseconds,
                      &result_microseconds, &result_nanoseconds, largest_unit);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalDuration>());
  CHECK(maybe_result.FromJust());

  // 17. Return ? CreateTemporalDuration(−roundResult.[[Years]],
  // −roundResult.[[Months]], −roundResult.[[Weeks]], result.[[Days]],
  // result.[[Hours]], result.[[Minutes]], result.[[Seconds]],
  // result.[[Milliseconds]], result.[[Microseconds]], result.[[Nanoseconds]]).

  return temporal::CreateTemporalDuration(
      isolate, sign * round_result.years, sign * round_result.months,
      sign * round_result.weeks, result_days, result_hours, result_minutes,
      result_seconds, result_milliseconds, result_microseconds,
      result_nanoseconds);
}

}  // namespace

// #sec-temporal.plaindatetime.prototype.until
MaybeHandle<JSTemporalDuration> JSTemporalPlainDateTime::Until(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> other_obj, Handle<Object> options_obj) {
  return UntilOrSince(isolate, date_time, other_obj, options_obj, 1,
                      "Temporal.PlainDateTime.prototype.until");
}

// #sec-temporal.plaindatetime.prototype.since
MaybeHandle<JSTemporalDuration> JSTemporalPlainDateTime::Since(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> other_obj, Handle<Object> options_obj) {
  return UntilOrSince(isolate, date_time, other_obj, options_obj, -1,
                      "Temporal.PlainDateTime.prototype.since");
}

// #sec-temporal.plaindatetime.prototype.round
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainDateTime::Round(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> options_obj) {
  const char* method = "Temporal.PlainDateTime.prototype.round";
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. If options is undefined, then
  if (options_obj->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainDateTime);
  }
  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainDateTime);
  // 5. Let smallestUnit be ? ToSmallestTemporalUnit(options, « "year", "month",
  // "week" », undefined).
  Maybe<temporal::Unit> maybe_smallest_unit = temporal::ToSmallestTemporalUnit(
      isolate, options,
      std::set<temporal::Unit>({temporal::Unit::kYear, temporal::Unit::kMonth,
                                temporal::Unit::kWeek}),
      temporal::Unit::kNotPresent, method);
  MAYBE_RETURN(maybe_smallest_unit, Handle<JSTemporalPlainDateTime>());
  temporal::Unit smallest_unit = maybe_smallest_unit.FromJust();
  // 6. If smallestUnit is undefined, throw a RangeError exception.
  if (smallest_unit == temporal::Unit::kNotPresent) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalPlainDateTime);
  }
  // 7. Let roundingMode be ? ToTemporalRoundingMode(options, "halfExpand").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(
          isolate, options, temporal::RoundingMode::kHalfExpand, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<JSTemporalPlainDateTime>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();
  // 8. Let roundingIncrement be ? ToTemporalDateTimeRoundingIncrement(options,
  // smallestUnit).
  Maybe<int> maybe_rounding_increment =
      temporal::ToTemporalDateTimeRoundingIncrement(isolate, options,
                                                    smallest_unit, method);
  MAYBE_RETURN(maybe_rounding_increment, Handle<JSTemporalPlainDateTime>());
  int rounding_increment = maybe_rounding_increment.FromJust();

  // 9. Let result be ! RoundISODateTime(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]], roundingIncrement, smallestUnit, roundingMode).
  temporal::DateTimeRecordCommon result = temporal::RoundISODateTime(
      isolate, date_time->iso_year(), date_time->iso_month(),
      date_time->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
      date_time->iso_second(), date_time->iso_millisecond(),
      date_time->iso_microsecond(), date_time->iso_nanosecond(),
      rounding_increment, smallest_unit, rounding_mode);
  // 10. Return ? CreateTemporalDateTime(result.[[Year]], result.[[Month]],
  // result.[[Day]], result.[[Hour]], result.[[Minute]], result.[[Second]],
  // result.[[Millisecond]], result.[[Microsecond]], result.[[Nanosecond]],
  // dateTime.[[Calendar]]).
  return temporal::CreateTemporalDateTime(
      isolate, result.year, result.month, result.day, result.hour,
      result.minute, result.second, result.millisecond, result.microsecond,
      result.nanosecond, Handle<JSReceiver>(date_time->calendar(), isolate));
}

// #sec-temporal.plaindatetime.prototype.equals
MaybeHandle<Oddball> JSTemporalPlainDateTime::Equals(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> other_obj) {
  const char* method = "Temporal.PlainDateTime.prototype.equals";
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Set other to ? ToTemporalDateTime(other).
  Handle<JSTemporalPlainDateTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, temporal::ToTemporalDateTime(isolate, other_obj, method),
      Oddball);
  // 4. Let result be ! CompareISODateTime(dateTime.[[ISOYear]],
  // dateTime.[[ISOMonth]], dateTime.[[ISODay]], dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]], other.[[ISOYear]], other.[[ISOMonth]],
  // other.[[ISODay]], other.[[ISOHour]], other.[[ISOMinute]],
  // other.[[ISOSecond]], other.[[ISOMillisecond]], other.[[ISOMicrosecond]],
  // other.[[ISONanosecond]]).
  int32_t result = temporal::CompareISODateTime(
      isolate, date_time->iso_year(), date_time->iso_month(),
      date_time->iso_day(), date_time->iso_hour(), date_time->iso_minute(),
      date_time->iso_second(), date_time->iso_millisecond(),
      date_time->iso_microsecond(), date_time->iso_nanosecond(),
      other->iso_year(), other->iso_month(), other->iso_day(),
      other->iso_hour(), other->iso_minute(), other->iso_second(),
      other->iso_millisecond(), other->iso_microsecond(),
      other->iso_nanosecond());
  // 5. If result is not 0, return false.
  if (result != 0) return isolate->factory()->false_value();
  // 6. Return ? CalendarEquals(dateTime.[[Calendar]], other.[[Calendar]]).
  return temporal::CalendarEquals(
      isolate, Handle<JSReceiver>(date_time->calendar(), isolate),
      Handle<JSReceiver>(other->calendar(), isolate));
}

// #sec-temporal.plaindatetime.prototype.tozoneddatetime
MaybeHandle<JSTemporalZonedDateTime> JSTemporalPlainDateTime::ToZonedDateTime(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time,
    Handle<Object> temporal_time_zone_like, Handle<Object> options_obj) {
  const char* method = "Temporal.PlainDateTime.prototype.toZonedDateTime";
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Let timeZone be ? ToTemporalTimeZone(temporalTimeZoneLike).
  Handle<JSReceiver> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      temporal::ToTemporalTimeZone(isolate, temporal_time_zone_like, method),
      JSTemporalZonedDateTime);
  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalZonedDateTime);
  // 5. Let disambiguation be ? ToTemporalDisambiguation(options).
  Maybe<temporal::Disambiguation> maybe_disambiguation =
      temporal::ToTemporalDisambiguation(isolate, options, method);
  MAYBE_RETURN(maybe_disambiguation, Handle<JSTemporalZonedDateTime>());
  temporal::Disambiguation disambiguation = maybe_disambiguation.FromJust();

  // 6. Let instant be ? BuiltinTimeZoneGetInstantFor(timeZone, dateTime,
  // disambiguation).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, instant,
                             BuiltinTimeZoneGetInstantFor(
                                 isolate, time_zone, date_time, disambiguation),
                             JSTemporalZonedDateTime);

  // 7. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // dateTime.[[Calendar]]).
  return temporal::CreateTemporalZonedDateTime(
      isolate, Handle<BigInt>(instant->nanoseconds(), isolate), time_zone,
      Handle<JSReceiver>(date_time->calendar(), isolate));
}

// #sec-temporal.plaindatetime.prototype.toplaindate
MaybeHandle<JSTemporalPlainDate> JSTemporalPlainDateTime::ToPlainDate(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time) {
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Return ? CreateTemporalDate(dateTime.[[ISOYear]], dateTime.[[ISOMonth]],
  // dateTime.[[ISODay]], dateTime.[[Calendar]]).
  return temporal::CreateTemporalDate(
      isolate, date_time->iso_year(), date_time->iso_month(),
      date_time->iso_day(), Handle<JSReceiver>(date_time->calendar(), isolate));
}

#define IMPL_TO_PLAIN(Name, r1, r2)                                           \
  MaybeHandle<JSTemporalPlain##Name> JSTemporalPlainDateTime::ToPlain##Name(  \
      Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time) {          \
    Factory* factory = isolate->factory();                                    \
    /* 1. Let dateTime be the this value.         */                          \
    /* 2. Perform ? RequireInternalSlot(dateTime, */                          \
    /* [[InitializedTemporalDateTime]]).          */                          \
    /* 3. Let calendar be dateTime.[[Calendar]].  */                          \
    Handle<JSReceiver> calendar =                                             \
        Handle<JSReceiver>(date_time->calendar(), isolate);                   \
    /* 4. Let fieldNames be ? CalendarFields(calendar, « #r1 , #r2 »). */   \
    Handle<FixedArray> field_names = factory->NewFixedArray(2);               \
    field_names->set(0, *(factory->r1##_string()));                           \
    field_names->set(1, *(factory->r2##_string()));                           \
    ASSIGN_RETURN_ON_EXCEPTION(                                               \
        isolate, field_names,                                                 \
        temporal::CalendarFields(isolate, calendar, field_names),             \
        JSTemporalPlain##Name);                                               \
    /* 5. Let fields be ? PrepareTemporalFields(dateTime, fieldNames,         \
     * «»).*/                                                               \
    Handle<JSReceiver> fields;                                                \
    ASSIGN_RETURN_ON_EXCEPTION(                                               \
        isolate, fields,                                                      \
        temporal::PrepareTemporalFields(isolate, date_time, field_names,      \
                                        false, false),                        \
        JSTemporalPlain##Name);                                               \
    /* 6. Return ? Name## FromFields(calendar, fields). */                    \
    return temporal::Name##FromFields(isolate, calendar, fields,              \
                                      isolate->factory()->undefined_value()); \
  }

// #sec-temporal.plaindatetime.prototype.toplainyearmonth
IMPL_TO_PLAIN(YearMonth, monthCode, year)
// #sec-temporal.plaindatetime.prototype.toplainmonthday
IMPL_TO_PLAIN(MonthDay, day, monthCode)
#undef IMPL_TO_PLAIN

// #sec-temporal.plaindatetime.prototype.toplaintime
MaybeHandle<JSTemporalPlainTime> JSTemporalPlainDateTime::ToPlainTime(
    Isolate* isolate, Handle<JSTemporalPlainDateTime> date_time) {
  // 1. Let dateTime be the this value.
  // 2. Perform ? RequireInternalSlot(dateTime,
  // [[InitializedTemporalDateTime]]).
  // 3. Return ? CreateTemporalTime(dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]]).
  return temporal::CreateTemporalTime(
      isolate, date_time->iso_hour(), date_time->iso_minute(),
      date_time->iso_second(), date_time->iso_millisecond(),
      date_time->iso_microsecond(), date_time->iso_nanosecond());
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

// #sec-temporal.plainmonthday.from
MaybeHandle<JSTemporalPlainMonthDay> JSTemporalPlainMonthDay::From(
    Isolate* isolate, Handle<Object> item, Handle<Object> options_obj) {
  const char* method = "Temporal.PlainMonthDay.from";
  // 1. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainMonthDay);
  // 2. If Type(item) is Object and item has an [[InitializedTemporalMonthDay]]
  // internal slot, then
  if (item->IsJSTemporalPlainMonthDay()) {
    // a. Perform ? ToTemporalOverflow(options).
    Maybe<temporal::ShowOverflow> maybe_overflow =
        temporal::ToTemporalOverflow(isolate, options, method);
    MAYBE_RETURN(maybe_overflow, Handle<JSTemporalPlainMonthDay>());
    // b. Return ? CreateTemporalMonthDay(item.[[ISOMonth]], item.[[ISODay]],
    // item.[[Calendar]], item.[[ISOYear]]).
    Handle<JSTemporalPlainMonthDay> month_day =
        Handle<JSTemporalPlainMonthDay>::cast(item);
    return temporal::CreateTemporalMonthDay(
        isolate, month_day->iso_month(), month_day->iso_day(),
        Handle<JSReceiver>(month_day->calendar(), isolate),
        month_day->iso_year());
  }
  // 3. Return ? ToTemporalMonthDay(item, options).
  return temporal::ToTemporalMonthDay(isolate, item, options, method);
}

// #sec-temporal.plainmonthday.prototype.with
MaybeHandle<JSTemporalPlainMonthDay> JSTemporalPlainMonthDay::With(
    Isolate* isolate, Handle<JSTemporalPlainMonthDay> month_day,
    Handle<Object> temporal_month_day_like_obj, Handle<Object> options_obj) {
  const char* method = "Temporal.PlainMonthDay.prototype.with";
  Factory* factory = isolate->factory();
  // 1. Let monthDay be the this value.
  // 2. Perform ? RequireInternalSlot(monthDay,
  // [[InitializedTemporalMonthDay]]).
  // 3. If Type(temporalMonthDayLike) is not Object, then
  if (!temporal_month_day_like_obj->IsJSReceiver()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainMonthDay);
  }
  Handle<JSReceiver> temporal_month_day_like =
      Handle<JSReceiver>::cast(temporal_month_day_like_obj);
  // 4. Perform ? RejectTemporalCalendarType(temporalMonthDayLike).
  Maybe<bool> maybe_reject =
      temporal::RejectTemporalCalendarType(isolate, temporal_month_day_like);

  MAYBE_RETURN(maybe_reject, Handle<JSTemporalPlainMonthDay>());

  // 5. Let calendarProperty be ? Get(temporalMonthDayLike, "calendar").
  Handle<Object> calendar_property;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_property,
      Object::GetPropertyOrElement(isolate, temporal_month_day_like,
                                   factory->calendar_string()),
      JSTemporalPlainMonthDay);

  // 6. If calendarProperty is not undefined, then
  if (!calendar_property->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainMonthDay);
  }
  // 7. Let timeZoneProperty be ? Get(temporalMonthDayLike, "timeZone").
  Handle<Object> time_zone_property;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone_property,
      Object::GetPropertyOrElement(isolate, temporal_month_day_like,
                                   factory->timeZone_string()),
      JSTemporalPlainMonthDay);
  // 8. If timeZoneProperty is not undefined, then
  if (!time_zone_property->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainMonthDay);
  }
  // 9. Let calendar be monthDay.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(month_day->calendar(), isolate);
  // 10. Let fieldNames be ? CalendarFields(calendar, « "day", "month",
  // "monthCode", "year" »).
  Handle<FixedArray> field_names = factory->NewFixedArray(4);
  int i = 0;
  field_names->set(i++, *(factory->day_string()));
  field_names->set(i++, *(factory->month_string()));
  field_names->set(i++, *(factory->monthCode_string()));
  field_names->set(i++, *(factory->year_string()));
  CHECK_EQ(i, 4);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, field_names,
      temporal::CalendarFields(isolate, calendar, field_names),
      JSTemporalPlainMonthDay);
  // 11. Let partialMonthDay be ?
  // PreparePartialTemporalFields(temporalMonthDayLike, fieldNames).
  Handle<JSReceiver> partial_month_day;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, partial_month_day,
                             temporal::PreparePartialTemporalFields(
                                 isolate, temporal_month_day_like, field_names),
                             JSTemporalPlainMonthDay);
  // 12. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainMonthDay);
  // 13. Let fields be ? PrepareTemporalFields(monthDay, fieldNames, «»).
  Handle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             temporal::PrepareTemporalFields(
                                 isolate, month_day, field_names, false, false),
                             JSTemporalPlainMonthDay);

  // 14. Set fields to ? CalendarMergeFields(calendar, fields, partialMonthDay).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             temporal::CalendarMergeFields(
                                 isolate, calendar, fields, partial_month_day),
                             JSTemporalPlainMonthDay);
  // 15. Set fields to ? PrepareTemporalFields(fields, fieldNames, «»).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             temporal::PrepareTemporalFields(
                                 isolate, fields, field_names, false, false),
                             JSTemporalPlainMonthDay);
  // 16. Return ? MonthDayFromFields(calendar, fields, options).
  return temporal::MonthDayFromFields(isolate, calendar, fields, options);
}

// #sec-temporal.plainyearmonth.prototype.equals
MaybeHandle<Oddball> JSTemporalPlainMonthDay::Equals(
    Isolate* isolate, Handle<JSTemporalPlainMonthDay> month_day,
    Handle<Object> other_obj) {
  const char* method = "Temporal.PlainMonthDay.prototype.equals";
  // 1. Let monthDay be the this value.
  // 2. Perform ? RequireInternalSlot(monthDay,
  // [[InitializedTemporalMonthDay]]).
  // 3. Set other to ? ToTemporalMonthDay(other).
  Handle<JSTemporalPlainMonthDay> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, temporal::ToTemporalMonthDay(isolate, other_obj, method),
      Oddball);
  // 4. If monthDay.[[ISOMonth]] ≠ other.[[ISOMonth]], return false.
  if (month_day->iso_month() != other->iso_month())
    return isolate->factory()->false_value();
  // 5. If monthDay.[[ISODay]] ≠ other.[[ISODay]], return false.
  if (month_day->iso_day() != other->iso_day())
    return isolate->factory()->false_value();
  // 6. If monthDay.[[ISOYear]] ≠ other.[[ISOYear]], return false.
  if (month_day->iso_year() != other->iso_year())
    return isolate->factory()->false_value();
  return temporal::CalendarEquals(
      isolate, Handle<JSReceiver>(month_day->calendar(), isolate),
      Handle<JSReceiver>(other->calendar(), isolate));
}

// #sec-temporal.plainmonthday.prototype.tostring
MaybeHandle<String> JSTemporalPlainMonthDay::ToString(
    Isolate* isolate, Handle<JSTemporalPlainMonthDay> month_day,
    Handle<Object> options_obj) {
  const char* method = "Temporal.PlainMonthDay.prototype.toString";
  //  1. Let monthDay be the this value.
  // 2. Perform ? RequireInternalSlot(monthDay,
  // [[InitializedTemporalMonthDay]]).
  // 3. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method), String);
  // 4. Let showCalendar be ? ToShowCalendarOption(options).
  Maybe<temporal::ShowCalendar> maybe_show_calendar =
      temporal::ToShowCalendarOption(isolate, options, method);
  MAYBE_RETURN(maybe_show_calendar, Handle<String>());
  temporal::ShowCalendar show_calendar = maybe_show_calendar.FromJust();
  // 5. Return ? TemporalMonthDayToString(monthDay, showCalendar).
  return TemporalMonthDayToString(
      isolate, month_day->iso_year(), month_day->iso_month(),
      month_day->iso_day(), Handle<JSReceiver>(month_day->calendar(), isolate),
      show_calendar);
}

// #sec-temporal.plainmonthday.prototype.tolocalestring
MaybeHandle<String> JSTemporalPlainMonthDay::ToLocaleString(
    Isolate* isolate, Handle<JSTemporalPlainMonthDay> month_day,
    Handle<Object> locales, Handle<Object> options) {
  return TemporalMonthDayToString(
      isolate, month_day->iso_year(), month_day->iso_month(),
      month_day->iso_day(), Handle<JSReceiver>(month_day->calendar(), isolate),
      temporal::ShowCalendar::kAuto);
}

// #sec-temporal.plainmonthday.prototype.tojson
MaybeHandle<String> JSTemporalPlainMonthDay::ToJSON(
    Isolate* isolate, Handle<JSTemporalPlainMonthDay> month_day) {
  return TemporalMonthDayToString(
      isolate, month_day->iso_year(), month_day->iso_month(),
      month_day->iso_day(), Handle<JSReceiver>(month_day->calendar(), isolate),
      temporal::ShowCalendar::kAuto);
}

// #sec-temporal.plainmonthday.prototype.toplaindate
MaybeHandle<JSTemporalPlainDate> JSTemporalPlainMonthDay::ToPlainDate(
    Isolate* isolate, Handle<JSTemporalPlainMonthDay> month_day,
    Handle<Object> item_obj) {
  Factory* factory = isolate->factory();
  // 1. Let monthDay be the this value.
  // 2. Perform ? RequireInternalSlot(monthDay,
  // [[InitializedTemporalMonthDay]]).
  // 3. If Type(item) is not Object, then
  if (!item_obj->IsJSReceiver()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainDate);
  }
  // 4. Let calendar be monthDay.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(month_day->calendar(), isolate);
  // 5. Let receiverFieldNames be ? CalendarFields(calendar, « "day",
  // "monthCode" »).
  Handle<FixedArray> receiver_field_names = factory->NewFixedArray(2);
  int i = 0;
  receiver_field_names->set(i++, *(factory->day_string()));
  receiver_field_names->set(i++, *(factory->monthCode_string()));
  CHECK_EQ(i, 2);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, receiver_field_names,
      temporal::CalendarFields(isolate, calendar, receiver_field_names),
      JSTemporalPlainDate);

  // 6. Let fields be ? PrepareTemporalFields(monthDay, receiverFieldNames, «»).
  Handle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      temporal::PrepareTemporalFields(isolate, month_day, receiver_field_names,
                                      false, false),
      JSTemporalPlainDate);

  Handle<JSReceiver> item = Handle<JSReceiver>::cast(item_obj);
  // 7. Let inputFieldNames be ? CalendarFields(calendar, « "year" »).
  Handle<FixedArray> input_field_names = factory->NewFixedArray(1);
  input_field_names->set(0, *(factory->year_string()));
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, input_field_names,
      temporal::CalendarFields(isolate, calendar, input_field_names),
      JSTemporalPlainDate);

  // 8. Let inputFields be ? PrepareTemporalFields(item, inputFieldNames, «»).
  Handle<JSReceiver> input_fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, input_fields,
      temporal::PrepareTemporalFields(isolate, item, input_field_names, false,
                                      false),
      JSTemporalPlainDate);
  // 9. Let mergedFields be ? CalendarMergeFields(calendar, fields,
  // inputFields).
  Handle<JSReceiver> merged_fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, merged_fields,
      temporal::CalendarMergeFields(isolate, calendar, fields, input_fields),
      JSTemporalPlainDate);

  // 10. Let mergedFieldNames be the List containing all the elements of
  // receiverFieldNames followed by all the elements of inputFieldNames, with
  // duplicate elements removed.
  Handle<FixedArray> merged_field_names = factory->NewFixedArray(
      receiver_field_names->length() + input_field_names->length());
  std::set<std::string> added;
  for (int j = 0; j < receiver_field_names->length(); j++) {
    Handle<Object> item = Handle<Object>(receiver_field_names->get(j), isolate);
    CHECK(item->IsString());
    Handle<String> string = Handle<String>::cast(item);
    if (added.find(string->ToCString().get()) == added.end()) {
      merged_field_names->set(static_cast<int>(added.size()), *item);
      added.insert(string->ToCString().get());
    }
  }
  for (int j = 0; j < input_field_names->length(); j++) {
    Handle<Object> item = Handle<Object>(input_field_names->get(j), isolate);
    CHECK(item->IsString());
    Handle<String> string = Handle<String>::cast(item);
    if (added.find(string->ToCString().get()) == added.end()) {
      merged_field_names->set(static_cast<int>(added.size()), *item);
      added.insert(string->ToCString().get());
    }
  }
  merged_field_names = FixedArray::ShrinkOrEmpty(
      isolate, merged_field_names, static_cast<int>(added.size()));

  // 11. Set mergedFields to ? PrepareTemporalFields(mergedFields,
  // mergedFieldNames, «»).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, merged_fields,
      temporal::PrepareTemporalFields(isolate, merged_fields,
                                      merged_field_names, false, false),
      JSTemporalPlainDate);
  // 12. Let options be ! OrdinaryObjectCreate(null).
  Handle<JSObject> options = factory->NewJSObjectWithNullProto();
  // 13. Perform ! CreateDataPropertyOrThrow(options, "overflow", "reject").
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->overflow_string(),
            factory->reject_string(), Just(kThrowOnError))
            .FromJust());

  // 14. Return ? DateFromFields(calendar, mergedFields, options).
  return temporal::DateFromFields(isolate, calendar, merged_fields, options);
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

// #sec-temporal.plainyearmonth.from
MaybeHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::From(
    Isolate* isolate, Handle<Object> item, Handle<Object> options_obj) {
  const char* method = "Temporal.PlainYearMonth.from";
  // 1. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainYearMonth);
  // 2. If Type(item) is Object and item has an [[InitializedTemporalYearMonth]]
  // internal slot, then
  if (item->IsJSTemporalPlainYearMonth()) {
    // a. Perform ? ToTemporalOverflow(options).
    Maybe<temporal::ShowOverflow> maybe_overflow =
        temporal::ToTemporalOverflow(isolate, options, method);
    MAYBE_RETURN(maybe_overflow, Handle<JSTemporalPlainYearMonth>());
    // b. Return ? CreateTemporalYearMonth(item.[[ISOYear]], item.[[ISOMonth]],
    // item.[[Calendar]], item.[[ISODay]]).
    Handle<JSTemporalPlainYearMonth> year_month =
        Handle<JSTemporalPlainYearMonth>::cast(item);
    return temporal::CreateTemporalYearMonth(
        isolate, year_month->iso_year(), year_month->iso_month(),
        Handle<JSReceiver>(year_month->calendar(), isolate),
        year_month->iso_day());
  }
  // 3. Return ? ToTemporalYearMonth(item, options).
  return temporal::ToTemporalYearMonth(isolate, item, options, method);
}

// #sec-temporal.plainyearmonth.compare
MaybeHandle<Smi> JSTemporalPlainYearMonth::Compare(Isolate* isolate,
                                                   Handle<Object> one_obj,
                                                   Handle<Object> two_obj) {
  const char* method = "Temporal.PlainYearMonth.compare";
  // 1. Set one to ? ToTemporalYearMonth(one).
  Handle<JSTemporalPlainYearMonth> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one, temporal::ToTemporalYearMonth(isolate, one_obj, method),
      Smi);
  // 2. Set two to ? ToTemporalYearMonth(two).
  Handle<JSTemporalPlainYearMonth> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two, temporal::ToTemporalYearMonth(isolate, two_obj, method),
      Smi);
  // 3. Return 𝔽(! CompareISODate(one.[[ISOYear]], one.[[ISOMonth]],
  // one.[[ISODay]], two.[[ISOYear]], two.[[ISOMonth]], two.[[ISODay]])).
  return Handle<Smi>(
      Smi::FromInt(temporal::CompareISODate(
          isolate, one->iso_year(), one->iso_month(), one->iso_day(),
          two->iso_year(), two->iso_month(), two->iso_day())),
      isolate);
}

// #sec-temporal.plainyearmonth.prototype.with
MaybeHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::With(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month,
    Handle<Object> temporal_year_month_like_obj, Handle<Object> options_obj) {
  Factory* factory = isolate->factory();
  const char* method = "Temporal.PlainYearMonth.prototype.with";
  // 1. Let yearMonth be the this value.
  // 2. Perform ? RequireInternalSlot(yearMonth,
  // [[InitializedTemporalYearMonth]]).
  // 3. If Type(temporalYearMonthLike) is not Object, then
  if (!temporal_year_month_like_obj->IsJSReceiver()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainYearMonth);
  }
  Handle<JSReceiver> temporal_year_month_like =
      Handle<JSReceiver>::cast(temporal_year_month_like_obj);
  // 4. Perform ? RejectTemporalCalendarType(temporalYearMonthLike).
  Maybe<bool> maybe_reject =
      temporal::RejectTemporalCalendarType(isolate, temporal_year_month_like);

  MAYBE_RETURN(maybe_reject, Handle<JSTemporalPlainYearMonth>());

  // 5. Let calendarProperty be ? Get(temporalYearMonthLike, "calendar").
  Handle<Object> calendar_property;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_property,
      Object::GetPropertyOrElement(isolate, temporal_year_month_like,
                                   factory->calendar_string()),
      JSTemporalPlainYearMonth);
  // 6. If calendarProperty is not undefined, then
  if (!calendar_property->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainYearMonth);
  }
  // 7. Let timeZoneProperty be ? Get(temporalYearMonthLike, "timeZone").
  Handle<Object> time_zone_property;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone_property,
      Object::GetPropertyOrElement(isolate, temporal_year_month_like,
                                   factory->timeZone_string()),
      JSTemporalPlainYearMonth);
  // 8. If timeZoneProperty is not undefined, then
  if (!time_zone_property->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainYearMonth);
  }
  // 9. Let calendar be yearMonth.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(year_month->calendar(), isolate);
  // 10. Let fieldNames be ? CalendarFields(calendar, « "month", "monthCode",
  // "year" »).
  Handle<FixedArray> field_names = factory->NewFixedArray(3);
  int i = 0;
  field_names->set(i++, *(factory->month_string()));
  field_names->set(i++, *(factory->monthCode_string()));
  field_names->set(i++, *(factory->year_string()));
  CHECK_EQ(i, 3);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, field_names,
      temporal::CalendarFields(isolate, calendar, field_names),
      JSTemporalPlainYearMonth);
  // 11. Let partialYearMonth be ?
  // PreparePartialTemporalFields(temporalYearMonthLike, fieldNames).
  Handle<JSReceiver> partial_year_month;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, partial_year_month,
      temporal::PreparePartialTemporalFields(isolate, temporal_year_month_like,
                                             field_names),
      JSTemporalPlainYearMonth);

  // 12. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainYearMonth);
  // 13. Let fields be ? PrepareTemporalFields(yearMonth, fieldNames, «»).
  Handle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      temporal::PrepareTemporalFields(isolate, year_month, field_names, false,
                                      false),
      JSTemporalPlainYearMonth);
  // 14. Set fields to ? CalendarMergeFields(calendar, fields,
  // partialYearMonth).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             temporal::CalendarMergeFields(
                                 isolate, calendar, fields, partial_year_month),
                             JSTemporalPlainYearMonth);
  // 15. Set fields to ? PrepareTemporalFields(fields, fieldNames, «»).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             temporal::PrepareTemporalFields(
                                 isolate, fields, field_names, false, false),
                             JSTemporalPlainYearMonth);
  // 16. Return ? YearMonthFromFields(calendar, fields, options).
  return temporal::YearMonthFromFields(isolate, calendar, fields, options);
}

namespace {
// #sec-temporal.plainyearmonth.prototype.add
// #sec-temporal.plainyearmonth.prototype.subrract
MaybeHandle<JSTemporalPlainYearMonth> AddOrSubtract(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month,
    Handle<Object> temporal_duration_like, Handle<Object> options_obj,
    int factor, const char* method) {
  Factory* factory = isolate->factory();
  // 1. Let yearMonth be the this value.
  // 2. Perform ? RequireInternalSlot(yearMonth,
  // [[InitializedTemporalYearMonth]]).
  // 3. Let duration be ? ToLimitedTemporalDuration(temporalDurationLike, « »).
  Maybe<temporal::DurationRecord> maybe_duration = ToLimitedTemporalDuration(
      isolate, temporal_duration_like, std::set<temporal::Unit>({}), method);
  MAYBE_RETURN(maybe_duration, Handle<JSTemporalPlainYearMonth>());
  temporal::DurationRecord duration = maybe_duration.FromJust();

  // 4. Let balanceResult be ? BalanceDuration(duration.[[Days]],
  // duration.[[Hours]], duration.[[Minutes]], duration.[[Seconds]],
  // duration.[[Milliseconds]], duration.[[Microseconds]],
  // duration.[[Nanoseconds]], "day").
  Maybe<bool> maybe_balance_result = BalanceDuration(
      isolate, &duration.days, &duration.hours, &duration.minutes,
      &duration.seconds, &duration.milliseconds, &duration.microseconds,
      &duration.nanoseconds, temporal::Unit::kDay);
  MAYBE_RETURN(maybe_balance_result, Handle<JSTemporalPlainYearMonth>());
  // 5. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainYearMonth);

  // 6. Let calendar be yearMonth.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(year_month->calendar(), isolate);

  // 7. Let fieldNames be ? CalendarFields(calendar, « "monthCode", "year" »).
  Handle<FixedArray> field_names = factory->NewFixedArray(2);
  int i = 0;
  field_names->set(i++, *(factory->monthCode_string()));
  field_names->set(i++, *(factory->year_string()));
  CHECK_EQ(i, 2);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, field_names,
      temporal::CalendarFields(isolate, calendar, field_names),
      JSTemporalPlainYearMonth);

  // 8. Let sign be ! DurationSign(duration.[[Years]], duration.[[Months]],
  // duration.[[Weeks]], balanceResult.[[Days]], 0, 0, 0, 0, 0, 0).
  duration.hours = duration.minutes = duration.seconds = duration.milliseconds =
      duration.microseconds = duration.nanoseconds = 0;
  int32_t sign = DurationSign(isolate, duration);
  int32_t day;
  // 9. If sign < 0, then
  if (sign * factor < 0) {
    // a. Let dayFromCalendar be ? CalendarDaysInMonth(calendar, yearMonth).
    Handle<Object> day_from_calendar;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, day_from_calendar,
        temporal::CalendarDaysInMonth(isolate, calendar, year_month),
        JSTemporalPlainYearMonth);

    // b. Let day be ? ToPositiveInteger(dayFromCalendar).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, day_from_calendar,
        temporal::ToPositiveInteger(isolate, day_from_calendar),
        JSTemporalPlainYearMonth);
    day = NumberToInt32(*day_from_calendar);
  } else {
    // 10. Else,
    // a. Let day be 1.
    day = 1;
  }
  // 11. Let date be ? CreateTemporalDate(yearMonth.[[ISOYear]],
  // yearMonth.[[ISOMonth]], day, calendar).
  Handle<JSTemporalPlainDate> date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date,
      temporal::CreateTemporalDate(isolate, year_month->iso_year(),
                                   year_month->iso_month(), day, calendar),
      JSTemporalPlainYearMonth);

  // 12. Let durationToAdd be ? CreateTemporalDuration(−duration.[[Years]],
  // −duration.[[Months]], −duration.[[Weeks]], −balanceResult.[[Days]], 0, 0,
  // 0, 0, 0, 0).
  Handle<JSTemporalDuration> duration_to_add;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, duration_to_add,
      temporal::CreateTemporalDuration(
          isolate, factor * duration.years, factor * duration.months,
          factor * duration.weeks, factor * duration.days, 0, 0, 0, 0, 0, 0),
      JSTemporalPlainYearMonth);

  // 13. Let optionsCopy be ! OrdinaryObjectCreate(%Object.prototype%).
  Handle<JSObject> options_copy =
      isolate->factory()->NewJSObject(isolate->object_function());

  // 14. Let entries be ? EnumerableOwnPropertyNames(options, key+value).
  // 15. For each element nextEntry of entries, do
  // a. Perform ! CreateDataPropertyOrThrow(optionsCopy, nextEntry[0],
  // nextEntry[1]).
  JSReceiver::SetOrCopyDataProperties(
      isolate, options_copy, options,
      PropertiesEnumerationMode::kEnumerationOrder)
      .Check();

  // 16. Let addedDate be ? CalendarDateAdd(calendar, date, durationToAdd,
  // options).
  Handle<JSTemporalPlainDate> added_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, added_date,
      temporal::CalendarDateAdd(isolate, calendar, date, duration_to_add,
                                options, isolate->factory()->undefined_value()),
      JSTemporalPlainYearMonth);
  // 17. Let addedDateFields be ? PrepareTemporalFields(addedDate, fieldNames,
  // «»).
  Handle<JSObject> added_date_fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, added_date_fields,
      temporal::PrepareTemporalFields(isolate, added_date, field_names, false,
                                      false),
      JSTemporalPlainYearMonth);

  // 18. Return ? YearMonthFromFields(calendar, addedDateFields, optionsCopy).
  return temporal::YearMonthFromFields(isolate, calendar, added_date_fields,
                                       options_copy);
}

}  // namespace

// #sec-temporal.plainyearmonth.prototype.add
MaybeHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::Add(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month,
    Handle<Object> temporal_duration_like, Handle<Object> options) {
  return AddOrSubtract(isolate, year_month, temporal_duration_like, options, 1,
                       "Temporal.PlainYearMonth.prototype.add");
}

// #sec-temporal.plainyearmonth.prototype.subtract
MaybeHandle<JSTemporalPlainYearMonth> JSTemporalPlainYearMonth::Subtract(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month,
    Handle<Object> temporal_duration_like, Handle<Object> options) {
  return AddOrSubtract(isolate, year_month, temporal_duration_like, options, -1,
                       "Temporal.PlainYearMonth.prototype.subtract");
}

namespace {
// #sec-temporal.plainyearmonth.prototype.until
// #sec-temporal.plainyearmonth.prototype.since
MaybeHandle<JSTemporalDuration> UntilOrSince(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month,
    Handle<Object> other_obj, Handle<Object> options_obj, int32_t sign,
    const char* method) {
  Factory* factory = isolate->factory();
  // 1. Let yearMonth be the this value.
  // 2. Perform ? RequireInternalSlot(yearMonth,
  // [[InitializedTemporalYearMonth]]).
  // 3. Set other to ? ToTemporalYearMonth(other).
  Handle<JSTemporalPlainYearMonth> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, temporal::ToTemporalYearMonth(isolate, other_obj, method),
      JSTemporalDuration);
  // 4. Let calendar be yearMonth.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(year_month->calendar(), isolate);
  // 5. If ? CalendarEquals(calendar, other.[[Calendar]]) is false, throw a
  // RangeError exception.
  Handle<Oddball> eq;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, eq,
      temporal::CalendarEquals(isolate, calendar,
                               Handle<JSReceiver>(other->calendar(), isolate)),
      JSTemporalDuration);
  if (eq->IsFalse()) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalDuration);
  }

  // 6. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalDuration);

  // 7. Let disallowedUnits be « "week", "day", "hour", "minute", "second",
  // "millisecond", "microsecond", "nanosecond" ».
  std::set<temporal::Unit> disallowed_units(
      {temporal::Unit::kWeek, temporal::Unit::kDay, temporal::Unit::kHour,
       temporal::Unit::kMinute, temporal::Unit::kSecond,
       temporal::Unit::kMillisecond, temporal::Unit::kMicrosecond,
       temporal::Unit::kNanosecond});
  // 8. Let smallestUnit be ? ToSmallestTemporalUnit(options, disallowedUnits,
  // "month").
  Maybe<temporal::Unit> maybe_smallest_unit = ToSmallestTemporalUnit(
      isolate, options, disallowed_units, temporal::Unit::kMonth, method);
  MAYBE_RETURN(maybe_smallest_unit, Handle<JSTemporalDuration>());
  temporal::Unit smallest_unit = maybe_smallest_unit.FromJust();

  // 9. Let largestUnit be ? ToLargestTemporalUnit(options, disallowedUnits,
  // "auto", "year").
  Maybe<temporal::Unit> maybe_largest_unit = ToLargestTemporalUnit(
      isolate, options, disallowed_units, temporal::Unit::kAuto,
      temporal::Unit::kYear, method);
  MAYBE_RETURN(maybe_largest_unit, Handle<JSTemporalDuration>());
  temporal::Unit largest_unit = maybe_largest_unit.FromJust();

  // 10. Perform ? ValidateTemporalUnitRange(largestUnit, smallestUnit).
  Maybe<bool> maybe_valid =
      ValidateTemporalUnitRange(isolate, largest_unit, smallest_unit, method);
  MAYBE_RETURN(maybe_valid, Handle<JSTemporalDuration>());
  CHECK(maybe_valid.FromJust());

  // 11. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(isolate, options,
                                       temporal::RoundingMode::kTrunc, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<JSTemporalDuration>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();

  // 12. Set roundingMode to ! NegateTemporalRoundingMode(roundingMode).
  if (sign == -1) {
    rounding_mode = NegateTemporalRoundingMode(isolate, rounding_mode);
  }

  // 13. Let roundingIncrement be ? ToTemporalRoundingIncrement(options,
  // undefined, false).
  Maybe<int> maybe_rounding_increment = temporal::ToTemporalRoundingIncrement(
      isolate, options, 0, false, false, method);
  MAYBE_RETURN(maybe_rounding_increment, Handle<JSTemporalDuration>());
  int rounding_increment = maybe_rounding_increment.FromJust();

  // 14. Let fieldNames be ? CalendarFields(calendar, « "monthCode", "year" »).
  Handle<FixedArray> field_names = factory->NewFixedArray(2);
  int i = 0;
  field_names->set(i++, *(factory->monthCode_string()));
  field_names->set(i++, *(factory->year_string()));
  CHECK_EQ(i, 2);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, field_names,
      temporal::CalendarFields(isolate, calendar, field_names),
      JSTemporalDuration);

  // 15. Let otherFields be ? PrepareTemporalFields(other, fieldNames, «»).
  Handle<JSObject> other_fields;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, other_fields,
                             temporal::PrepareTemporalFields(
                                 isolate, other, field_names, false, false),
                             JSTemporalDuration);

  // 16. Perform ! CreateDataPropertyOrThrow(otherFields, "day", 1𝔽).
  CHECK(JSReceiver::CreateDataProperty(
            isolate, other_fields, factory->day_string(), factory->NewNumber(1),
            Just(kThrowOnError))
            .FromJust());

  // 17. Let otherDate be ? DateFromFields(calendar, otherFields).
  Handle<JSTemporalPlainDate> other_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other_date,
      temporal::DateFromFields(isolate, calendar, other_fields,
                               factory->undefined_value()),
      JSTemporalDuration);

  // 18. Let thisFields be ? PrepareTemporalFields(yearMonth, fieldNames, «»).
  Handle<JSObject> this_fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, this_fields,
      temporal::PrepareTemporalFields(isolate, year_month, field_names, false,
                                      false),
      JSTemporalDuration);

  // 19. Perform ! CreateDataPropertyOrThrow(thisFields, "day", 1𝔽).
  CHECK(JSReceiver::CreateDataProperty(
            isolate, this_fields, factory->day_string(), factory->NewNumber(1),
            Just(kThrowOnError))
            .FromJust());

  // 20. Let thisDate be ? DateFromFields(calendar, thisFields).
  Handle<JSTemporalPlainDate> this_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, this_date,
      temporal::DateFromFields(isolate, calendar, this_fields,
                               factory->undefined_value()),
      JSTemporalDuration);

  // 21. Let untilOptions be ? MergeLargestUnitOption(options, largestUnit).
  Handle<JSObject> until_options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, until_options,
      temporal::MergeLargestUnitOption(isolate, options, largest_unit),
      JSTemporalDuration);

  // 22. Let result be ? CalendarDateUntil(calendar, thisDate, otherDate,
  // untilOptions).
  Handle<JSTemporalDuration> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result,
      temporal::CalendarDateUntil(isolate, calendar, this_date, other_date,
                                  until_options),
      JSTemporalDuration);

  // 23. If smallestUnit is "month" and roundingIncrement = 1, then
  if (smallest_unit == temporal::Unit::kMonth && rounding_increment == 1) {
    // Temporal.PlainYearMonth.prototype.since

    // a. Return ? CreateTemporalDuration(−result.[[Years]], −result.[[Months]],
    // 0, 0, 0, 0, 0, 0, 0, 0).

    // Temporal.PlainYearMonth.prototype.until

    // a. Return ? CreateTemporalDuration(result.[[Years]], result.[[Months]],
    // 0, 0, 0, 0, 0, 0, 0, 0).
    return temporal::CreateTemporalDuration(
        isolate, sign * result->years().Number(),
        sign * result->months().Number(), 0, 0, 0, 0, 0, 0, 0, 0);
  }
  // 24. Let relativeTo be ? CreateTemporalDateTime(thisDate.[[ISOYear]],
  // thisDate.[[ISOMonth]], thisDate.[[ISODay]], 0, 0, 0, 0, 0, 0, calendar).
  Handle<JSTemporalPlainDateTime> relative_to;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, relative_to,
      temporal::CreateTemporalDateTime(
          isolate, this_date->iso_year(), this_date->iso_month(),
          this_date->iso_day(), 0, 0, 0, 0, 0, 0, calendar),
      JSTemporalDuration);
  // XX 25. Set roundingMode to ! NegateTemporalRoundingMode(roundingMode).
  // EXTRA See https://github.com/tc39/proposal-temporal/pull/1777

  // 26. Let result be ? RoundDuration(result.[[Years]], result.[[Months]], 0,
  // 0, 0, 0, 0, 0, 0, 0, roundingIncrement, smallestUnit, roundingMode,
  // relativeTo).
  //
  double remainder;
  Maybe<temporal::DurationRecord> maybe_round_result =
      RoundDuration(isolate,
                    {result->years().Number(), result->months().Number(), 0, 0,
                     0, 0, 0, 0, 0, 0},
                    rounding_increment, smallest_unit, rounding_mode,
                    relative_to, &remainder, method);
  MAYBE_RETURN(maybe_round_result, Handle<JSTemporalDuration>());
  temporal::DurationRecord round_result = maybe_round_result.FromJust();

  // 27. Return ? CreateTemporalDuration(−result.[[Years]], −result.[[Months]],
  // 0, 0, 0, 0, 0, 0, 0, 0).
  return temporal::CreateTemporalDuration(isolate, sign * round_result.years,
                                          sign * round_result.months, 0, 0, 0,
                                          0, 0, 0, 0, 0);
}

}  // namespace

// #sec-temporal.plainyearmonth.prototype.until
MaybeHandle<JSTemporalDuration> JSTemporalPlainYearMonth::Until(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month,
    Handle<Object> other_obj, Handle<Object> options_obj) {
  return UntilOrSince(isolate, year_month, other_obj, options_obj, 1,
                      "Temporal.PlainYearMonth.prototype.until");
}

// #sec-temporal.plainyearmonth.prototype.since
MaybeHandle<JSTemporalDuration> JSTemporalPlainYearMonth::Since(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month,
    Handle<Object> other_obj, Handle<Object> options_obj) {
  return UntilOrSince(isolate, year_month, other_obj, options_obj, -1,
                      "Temporal.PlainYearMonth.prototype.since");
}

// #sec-temporal.plainyearmonth.prototype.equals
MaybeHandle<Oddball> JSTemporalPlainYearMonth::Equals(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month,
    Handle<Object> other_obj) {
  const char* method = "Temporal.PlainYearMonth.prototype.equals";
  // 1. Let yearMonth be the this value.
  // 2. Perform ? RequireInternalSlot(yearMonth,
  // [[InitializedTemporalYearMonth]]).
  // 3. Set other to ? ToTemporalYearMonth(other).
  Handle<JSTemporalPlainYearMonth> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, temporal::ToTemporalYearMonth(isolate, other_obj, method),
      Oddball);
  // 4. If yearMonth.[[ISOYear]] ≠ other.[[ISOYear]], return false.
  if (year_month->iso_year() != other->iso_year())
    return isolate->factory()->false_value();
  // 5. If yearMonth.[[ISOMonth]] ≠ other.[[ISOMonth]], return false.
  if (year_month->iso_month() != other->iso_month())
    return isolate->factory()->false_value();
  // 6. If yearMonth.[[ISODay]] ≠ other.[[ISODay]], return false.
  if (year_month->iso_day() != other->iso_day())
    return isolate->factory()->false_value();
  // 7. Return ? CalendarEquals(yearMonth.[[Calendar]], other.[[Calendar]]).
  return temporal::CalendarEquals(
      isolate, Handle<JSReceiver>(year_month->calendar(), isolate),
      Handle<JSReceiver>(other->calendar(), isolate));
}

// #sec-temporal.plainyearmonth.prototype.tostring
MaybeHandle<String> JSTemporalPlainYearMonth::ToString(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month,
    Handle<Object> options_obj) {
  const char* method = "Temporal.PlainYearMonth.prototype.toString";
  // 1. Let yearMonth be the this value.
  // 2. Perform ? RequireInternalSlot(yearMonth,
  // [[InitializedTemporalYearMonth]]).
  // 3. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method), String);
  // 4. Let showCalendar be ? ToShowCalendarOption(options).
  Maybe<temporal::ShowCalendar> maybe_show_calendar =
      temporal::ToShowCalendarOption(isolate, options, method);
  MAYBE_RETURN(maybe_show_calendar, Handle<String>());
  temporal::ShowCalendar show_calendar = maybe_show_calendar.FromJust();

  // 5. Return ? TemporalYearMonthToString(yearMonth, showCalendar).
  return TemporalYearMonthToString(
      isolate, year_month->iso_year(), year_month->iso_month(),
      year_month->iso_day(),
      Handle<JSReceiver>(year_month->calendar(), isolate), show_calendar);
}

// #sec-temporal.plainyearmonth.prototype.tolocalestring
MaybeHandle<String> JSTemporalPlainYearMonth::ToLocaleString(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month,
    Handle<Object> locales, Handle<Object> options) {
  return TemporalYearMonthToString(
      isolate, year_month->iso_year(), year_month->iso_month(),
      year_month->iso_day(),
      Handle<JSReceiver>(year_month->calendar(), isolate),
      temporal::ShowCalendar::kAuto);
}

// #sec-temporal.plainyearmonth.prototype.tojson
MaybeHandle<String> JSTemporalPlainYearMonth::ToJSON(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month) {
  return TemporalYearMonthToString(
      isolate, year_month->iso_year(), year_month->iso_month(),
      year_month->iso_day(),
      Handle<JSReceiver>(year_month->calendar(), isolate),
      temporal::ShowCalendar::kAuto);
}

// #sec-temporal.plainyearmonth.prototype.toplaindate
MaybeHandle<JSTemporalPlainDate> JSTemporalPlainYearMonth::ToPlainDate(
    Isolate* isolate, Handle<JSTemporalPlainYearMonth> year_month,
    Handle<Object> item_obj) {
  Factory* factory = isolate->factory();
  // 1. Let yearMonth be the this value.
  // 2. Perform ? RequireInternalSlot(yearMonth,
  // [[InitializedTemporalYearMonth]]).
  // 3. If Type(item) is not Object, then
  if (!item_obj->IsJSReceiver()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainDate);
  }
  Handle<JSReceiver> item = Handle<JSReceiver>::cast(item_obj);
  // 4. Let calendar be yearMonth.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(year_month->calendar(), isolate);

  // 5. Let receiverFieldNames be ? CalendarFields(calendar, « "monthCode",
  // "year" »).
  Handle<FixedArray> receiver_field_names = factory->NewFixedArray(2);
  int i = 0;
  receiver_field_names->set(i++, *(factory->monthCode_string()));
  receiver_field_names->set(i++, *(factory->year_string()));
  CHECK_EQ(i, 2);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, receiver_field_names,
      temporal::CalendarFields(isolate, calendar, receiver_field_names),
      JSTemporalPlainDate);

  // 6. Let fields be ? PrepareTemporalFields(yearMonth, receiverFieldNames,
  // «»).
  Handle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      temporal::PrepareTemporalFields(isolate, year_month, receiver_field_names,
                                      false, false),
      JSTemporalPlainDate);

  // 7. Let inputFieldNames be ? CalendarFields(calendar, « "day" »).
  Handle<FixedArray> input_field_names = factory->NewFixedArray(1);
  input_field_names->set(0, *(factory->day_string()));
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, input_field_names,
      temporal::CalendarFields(isolate, calendar, input_field_names),
      JSTemporalPlainDate);

  // 8. Let inputFields be ? PrepareTemporalFields(item, inputFieldNames, «»).
  Handle<JSReceiver> input_fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, input_fields,
      temporal::PrepareTemporalFields(isolate, item, input_field_names, false,
                                      false),
      JSTemporalPlainDate);
  // 9. Let mergedFields be ? CalendarMergeFields(calendar, fields,
  // inputFields).
  Handle<JSReceiver> merged_fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, merged_fields,
      temporal::CalendarMergeFields(isolate, calendar, fields, input_fields),
      JSTemporalPlainDate);
  // 10. Let mergedFieldNames be the List containing all the elements of
  // receiverFieldNames followed by all the elements of inputFieldNames, with
  // duplicate elements removed.
  Handle<FixedArray> merged_field_names = factory->NewFixedArray(
      receiver_field_names->length() + input_field_names->length());
  std::set<std::string> added;
  for (int j = 0; j < receiver_field_names->length(); j++) {
    Handle<Object> item = Handle<Object>(receiver_field_names->get(j), isolate);
    CHECK(item->IsString());
    Handle<String> string = Handle<String>::cast(item);
    if (added.find(string->ToCString().get()) == added.end()) {
      merged_field_names->set(static_cast<int>(added.size()), *item);
      added.insert(string->ToCString().get());
    }
  }
  for (int j = 0; j < input_field_names->length(); j++) {
    Handle<Object> item = Handle<Object>(input_field_names->get(j), isolate);
    CHECK(item->IsString());
    Handle<String> string = Handle<String>::cast(item);
    if (added.find(string->ToCString().get()) == added.end()) {
      merged_field_names->set(static_cast<int>(added.size()), *item);
      added.insert(string->ToCString().get());
    }
  }
  merged_field_names = FixedArray::ShrinkOrEmpty(
      isolate, merged_field_names, static_cast<int>(added.size()));

  // 11. Set mergedFields to ? PrepareTemporalFields(mergedFields,
  // mergedFieldNames, «»).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, merged_fields,
      temporal::PrepareTemporalFields(isolate, merged_fields,
                                      merged_field_names, false, false),
      JSTemporalPlainDate);
  // 12. Let options be ! OrdinaryObjectCreate(null).
  Handle<JSObject> options = factory->NewJSObjectWithNullProto();
  // 13. Perform ! CreateDataPropertyOrThrow(options, "overflow", "reject").
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->overflow_string(),
            factory->reject_string(), Just(kThrowOnError))
            .FromJust());
  // 14. Return ? DateFromFields(calendar, mergedFields, options).
  return temporal::DateFromFields(isolate, calendar, merged_fields, options);
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

// #sec-temporal.now.plaintimeiso
MaybeHandle<JSTemporalPlainTime> JSTemporalPlainTime::NowISO(
    Isolate* isolate, Handle<Object> temporal_time_zone_like) {
  const char* method = "Temporal.Now.plainTimeISO";
  // 1. Let calendar be ! GetISO8601Calendar().
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             temporal::GetISO8601Calendar(isolate),
                             JSTemporalPlainTime);
  // 2. Let dateTime be ? SystemDateTime(temporalTimeZoneLike, calendar).
  Handle<JSTemporalPlainDateTime> date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time,
      temporal::SystemDateTime(isolate, temporal_time_zone_like, calendar,
                               method),
      JSTemporalPlainTime);
  // 3. Return ! CreateTemporalTime(dateTime.[[ISOHour]],
  // dateTime.[[ISOMinute]], dateTime.[[ISOSecond]],
  // dateTime.[[ISOMillisecond]], dateTime.[[ISOMicrosecond]],
  // dateTime.[[ISONanosecond]]).
  return temporal::CreateTemporalTime(
      isolate, date_time->iso_hour(), date_time->iso_minute(),
      date_time->iso_second(), date_time->iso_millisecond(),
      date_time->iso_microsecond(), date_time->iso_nanosecond());
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

// #sec-temporal.plaintime.from
MaybeHandle<JSTemporalPlainTime> JSTemporalPlainTime::From(
    Isolate* isolate, Handle<Object> item_obj, Handle<Object> options_obj) {
  const char* method = "Temporal.PlainTime.from";
  // 1. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainTime);
  // 2. Let overflow be ? ToTemporalOverflow(options).
  Maybe<temporal::ShowOverflow> maybe_overflow =
      temporal::ToTemporalOverflow(isolate, options, method);
  MAYBE_RETURN(maybe_overflow, Handle<JSTemporalPlainTime>());
  temporal::ShowOverflow overflow = maybe_overflow.FromJust();
  // 3. If Type(item) is Object and item has an [[InitializedTemporalTime]]
  // internal slot, then
  if (item_obj->IsJSTemporalPlainTime()) {
    // a. Return ? CreateTemporalTime(item.[[ISOHour]], item.[[ISOMinute]],
    // item.[[ISOSecond]], item.[[ISOMillisecond]], item.[[ISOMicrosecond]],
    // item.[[ISONanosecond]]).
    Handle<JSTemporalPlainTime> item =
        Handle<JSTemporalPlainTime>::cast(item_obj);
    return temporal::CreateTemporalTime(
        isolate, item->iso_hour(), item->iso_minute(), item->iso_second(),
        item->iso_millisecond(), item->iso_microsecond(),
        item->iso_nanosecond());
  }
  // 4. Return ? ToTemporalTime(item, overflow).
  return temporal::ToTemporalTime(isolate, item_obj, overflow, method);
}

// #sec-temporal.plaintime.compare
MaybeHandle<Smi> JSTemporalPlainTime::Compare(Isolate* isolate,
                                              Handle<Object> one_obj,
                                              Handle<Object> two_obj) {
  const char* method = "Temporal.PainTime.compare";
  // 1. Set one to ? ToTemporalTime(one).
  Handle<JSTemporalPlainTime> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one,
      temporal::ToTemporalTime(isolate, one_obj,
                               temporal::ShowOverflow::kConstrain, method),
      Smi);
  // 2. Set two to ? ToTemporalTime(two).
  Handle<JSTemporalPlainTime> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two,
      temporal::ToTemporalTime(isolate, two_obj,
                               temporal::ShowOverflow::kConstrain, method),
      Smi);
  // 3. Return 𝔽(! CompareTemporalTime(one.[[ISOHour]], one.[[ISOMinute]],
  // one.[[ISOSecond]], one.[[ISOMillisecond]], one.[[ISOMicrosecond]],
  // one.[[ISONanosecond]], two.[[ISOHour]], two.[[ISOMinute]],
  // two.[[ISOSecond]], two.[[ISOMillisecond]], two.[[ISOMicrosecond]],
  // two.[[ISONanosecond]])).
  return Handle<Smi>(
      Smi::FromInt(temporal::CompareTemporalTime(
          isolate, one->iso_hour(), one->iso_minute(), one->iso_second(),
          one->iso_millisecond(), one->iso_microsecond(), one->iso_nanosecond(),
          two->iso_hour(), two->iso_minute(), two->iso_second(),
          two->iso_millisecond(), two->iso_microsecond(),
          two->iso_nanosecond())),
      isolate);
}

namespace {
// #sec-temporal.plaintime.prototype.add
// #sec-temporal.plaintime.prototype.subtract
MaybeHandle<JSTemporalPlainTime> AddOrSubtract(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> temporal_duration_like, int factor, const char* method) {
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. Let duration be ? ToLimitedTemporalDuration(temporalDurationLike, « »).
  Maybe<temporal::DurationRecord> maybe_duration =
      temporal::ToLimitedTemporalDuration(isolate, temporal_duration_like,
                                          std::set<temporal::Unit>({}), method);
  MAYBE_RETURN(maybe_duration, Handle<JSTemporalPlainTime>());
  temporal::DurationRecord duration = maybe_duration.FromJust();
  // 4. Let result be ! AddTime(temporalTime.[[ISOHour]],
  // temporalTime.[[ISOMinute]], temporalTime.[[ISOSecond]],
  // temporalTime.[[ISOMillisecond]], temporalTime.[[ISOMicrosecond]],
  // temporalTime.[[ISONanosecond]], −duration.[[Hours]], −duration.[[Minutes]],
  // −duration.[[Seconds]], −duration.[[Milliseconds]],
  // −duration.[[Microseconds]], −duration.[[Nanoseconds]]).
  temporal::DateTimeRecordCommon result = temporal::AddTime(
      isolate, temporal_time->iso_hour(), temporal_time->iso_minute(),
      temporal_time->iso_second(), temporal_time->iso_millisecond(),
      temporal_time->iso_microsecond(), temporal_time->iso_nanosecond(),
      factor * duration.hours, factor * duration.minutes,
      factor * duration.seconds, factor * duration.milliseconds,
      factor * duration.microseconds, factor * duration.nanoseconds);
  // 5. Assert: ! IsValidTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]) is true.
  CHECK(temporal::IsValidTime(isolate, result.hour, result.minute,
                              result.second, result.millisecond,
                              result.microsecond, result.nanosecond));
  // 6. Return ? CreateTemporalTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]).
  return temporal::CreateTemporalTime(isolate, result.hour, result.minute,
                                      result.second, result.millisecond,
                                      result.microsecond, result.nanosecond);
}
}  // namespace

// #sec-temporal.plaintime.prototype.add
MaybeHandle<JSTemporalPlainTime> JSTemporalPlainTime::Add(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> temporal_duration_like) {
  return AddOrSubtract(isolate, temporal_time, temporal_duration_like, 1,
                       "Temporal.PlainTime.prototype.add");
}

// #sec-temporal.plaintime.prototype.subtract
MaybeHandle<JSTemporalPlainTime> JSTemporalPlainTime::Subtract(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> temporal_duration_like) {
  return AddOrSubtract(isolate, temporal_time, temporal_duration_like, -1,
                       "Temporal.PlainTime.prototype.subtract");
}

// #sec-temporal.plaintime.prototype.with
MaybeHandle<JSTemporalPlainTime> JSTemporalPlainTime::With(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> temporal_time_like_obj, Handle<Object> options_obj) {
  Factory* factory = isolate->factory();
  const char* method = "Temporal.PlainTime.prototype.with";
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. If Type(temporalTimeLike) is not Object, then
  if (!temporal_time_like_obj->IsJSReceiver()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainTime);
  }
  Handle<JSReceiver> temporal_time_like =
      Handle<JSReceiver>::cast(temporal_time_like_obj);
  // 4. Perform ? RejectTemporalCalendarType(temporalTimeLike).
  Maybe<bool> maybe_reject =
      temporal::RejectTemporalCalendarType(isolate, temporal_time_like);

  MAYBE_RETURN(maybe_reject, Handle<JSTemporalPlainTime>());
  // 5. Let calendarProperty be ? Get(temporalTimeLike, "calendar").
  Handle<Object> calendar_property;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_property,
      Object::GetPropertyOrElement(isolate, temporal_time_like,
                                   factory->calendar_string()),
      JSTemporalPlainTime);
  // 6. If calendarProperty is not undefined, then
  if (!calendar_property->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainTime);
  }
  // 7. Let timeZoneProperty be ? Get(temporalTimeLike, "timeZone").
  Handle<Object> time_zone_property;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone_property,
      Object::GetPropertyOrElement(isolate, temporal_time_like,
                                   factory->timeZone_string()),
      JSTemporalPlainTime);
  // 8. If timeZoneProperty is not undefined, then
  if (!time_zone_property->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainTime);
  }
  // 9. Let partialTime be ? ToPartialTime(temporalTimeLike).
  // step 12-24 below
  bool any = false;
#define GET_PROP(name)                                                         \
  int32_t name = temporal_time->iso_##name();                                  \
  {                                                                            \
    Handle<Object> value;                                                      \
    ASSIGN_RETURN_ON_EXCEPTION(                                                \
        isolate, value,                                                        \
        JSReceiver::GetProperty(isolate, temporal_time_like,                   \
                                factory->name##_string()),                     \
        JSTemporalPlainTime);                                                  \
    if (!value->IsUndefined()) {                                               \
      Handle<Object> number;                                                   \
      ASSIGN_RETURN_ON_EXCEPTION(                                              \
          isolate, number, temporal::ToIntegerThrowOnInfinity(isolate, value), \
          JSTemporalPlainTime);                                                \
      name = NumberToInt32(*number);                                           \
      any = true;                                                              \
    }                                                                          \
  }
  GET_PROP(hour);
  GET_PROP(microsecond);
  GET_PROP(millisecond);
  GET_PROP(minute);
  GET_PROP(nanosecond);
  GET_PROP(second);
#undef GET_PROP

  // 5. If any is false, then
  if (!any) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainTime);
  }

  // 10. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainTime);
  // 11. Let overflow be ? ToTemporalOverflow(options).
  Maybe<temporal::ShowOverflow> maybe_overflow =
      temporal::ToTemporalOverflow(isolate, options, method);
  MAYBE_RETURN(maybe_overflow, Handle<JSTemporalPlainTime>());

  // 24. Let result be ? RegulateTime(hour, minute, second, millisecond,
  // microsecond, nanosecond, overflow).
  Maybe<bool> maybe_result =
      RegulateTime(isolate, &hour, &minute, &second, &millisecond, &microsecond,
                   &nanosecond, maybe_overflow.FromJust());
  MAYBE_RETURN(maybe_result, Handle<JSTemporalPlainTime>());
  // 25. Return ? CreateTemporalTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]).
  return temporal::CreateTemporalTime(isolate, hour, minute, second,
                                      millisecond, microsecond, nanosecond);
}

namespace {

MaybeHandle<JSTemporalDuration> UntilOrSince(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> other_obj, Handle<Object> options_obj, int32_t sign,
    const char* method) {
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. Set other to ? ToTemporalTime(other).
  Handle<JSTemporalPlainTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, temporal::ToTemporalTime(isolate, other_obj, method),
      JSTemporalDuration);

  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalDuration);

  // 5. Let smallestUnit be ? ToSmallestTemporalUnit(options, « "year", "month",
  // "week", "day" », "nanosecond").
  std::set<temporal::Unit> disallowed_units(
      {temporal::Unit::kYear, temporal::Unit::kMonth, temporal::Unit::kWeek,
       temporal::Unit::kDay});
  Maybe<temporal::Unit> maybe_smallest_unit = ToSmallestTemporalUnit(
      isolate, options, disallowed_units, temporal::Unit::kNanosecond, method);
  MAYBE_RETURN(maybe_smallest_unit, Handle<JSTemporalDuration>());
  temporal::Unit smallest_unit = maybe_smallest_unit.FromJust();

  // 6. Let largestUnit be ? ToLargestTemporalUnit(options, « "year", "month",
  // "week", "day" », "auto", "hour").
  Maybe<temporal::Unit> maybe_largest_unit = ToLargestTemporalUnit(
      isolate, options, disallowed_units, temporal::Unit::kAuto,
      temporal::Unit::kHour, method);
  MAYBE_RETURN(maybe_largest_unit, Handle<JSTemporalDuration>());
  temporal::Unit largest_unit = maybe_largest_unit.FromJust();

  // 7. Perform ? ValidateTemporalUnitRange(largestUnit, smallestUnit).
  Maybe<bool> maybe_valid = temporal::ValidateTemporalUnitRange(
      isolate, largest_unit, smallest_unit, method);
  MAYBE_RETURN(maybe_valid, Handle<JSTemporalDuration>());
  CHECK(maybe_valid.FromJust());

  // 8. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(isolate, options,
                                       temporal::RoundingMode::kTrunc, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<JSTemporalDuration>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();

  // 9. Set roundingMode to ! NegateTemporalRoundingMode(roundingMode).
  if (sign == -1) {
    rounding_mode = NegateTemporalRoundingMode(isolate, rounding_mode);
  }

  // 10. Let maximum be !
  // MaximumTemporalDurationRoundingIncrement(smallestUnit).
  double maximum;
  Maybe<bool> maybe_maximum = MaximumTemporalDurationRoundingIncrement(
      isolate, smallest_unit, &maximum);
  MAYBE_RETURN(maybe_maximum, Handle<JSTemporalDuration>());
  bool maximum_is_defined = maybe_maximum.FromJust();

  // 11. Let roundingIncrement be ? ToTemporalRoundingIncrement(options,
  // maximum, false).
  Maybe<int> maybe_rounding_increment = temporal::ToTemporalRoundingIncrement(
      isolate, options, maximum, maximum_is_defined, false, method);
  MAYBE_RETURN(maybe_rounding_increment, Handle<JSTemporalDuration>());
  int rounding_increment = maybe_rounding_increment.FromJust();

  Handle<JSTemporalPlainTime> first = (sign == -1) ? other : temporal_time;
  Handle<JSTemporalPlainTime> second = (sign == -1) ? temporal_time : other;

  // Temporal.PlainTime.prototype.since

  // 12. Let result be ! DifferenceTime(temporalTime.[[ISOHour]],
  // temporalTime.[[ISOMinute]], temporalTime.[[ISOSecond]],
  // temporalTime.[[ISOMillisecond]], temporalTime.[[ISOMicrosecond]],
  // temporalTime.[[ISONanosecond]], other.[[ISOHour]], other.[[ISOMinute]],
  // other.[[ISOSecond]], other.[[ISOMillisecond]], other.[[ISOMicrosecond]],
  // other.[[ISONanosecond]]).

  // Temporal.PlainTime.prototype.until

  // 11. Let result be ! DifferenceTime(other.[[ISOHour]], other.[[ISOMinute]],
  // other.[[ISOSecond]], other.[[ISOMillisecond]], other.[[ISOMicrosecond]],
  // other.[[ISONanosecond]], temporalTime.[[ISOHour]],
  // temporalTime.[[ISOMinute]], temporalTime.[[ISOSecond]],
  // temporalTime.[[ISOMillisecond]], temporalTime.[[ISOMicrosecond]],
  // temporalTime.[[ISONanosecond]]).
  temporal::DurationRecord result = temporal::DifferenceTime(
      isolate, first->iso_hour(), first->iso_minute(), first->iso_second(),
      first->iso_millisecond(), first->iso_microsecond(),
      first->iso_nanosecond(), second->iso_hour(), second->iso_minute(),
      second->iso_second(), second->iso_millisecond(),
      second->iso_microsecond(), second->iso_nanosecond());

  // Temporal.PlainTime.prototype.since

  // 13. Set result to ? RoundDuration(0, 0, 0, 0, −result.[[Hours]],
  // −result.[[Minutes]], −result.[[Seconds]], −result.[[Milliseconds]],
  // −result.[[Microseconds]], −result.[[Nanoseconds]], roundingIncrement,
  // smallestUnit, roundingMode).

  // Temporal.PlainTime.prototype.until

  // 12. Set result to ? RoundDuration(0, 0, 0, 0, result.[[Hours]],
  // result.[[Minutes]], result.[[Seconds]], result.[[Milliseconds]],
  // result.[[Microseconds]], result.[[Nanoseconds]], roundingIncrement,
  // smallestUnit, roundingMode).
  double remainder;
  Maybe<temporal::DurationRecord> maybe_result = temporal::RoundDuration(
      isolate,
      {0, 0, 0, 0, sign * result.hours, sign * result.minutes,
       sign * result.seconds, sign * result.milliseconds,
       sign * result.microseconds, sign * result.nanoseconds},
      rounding_increment, smallest_unit, rounding_mode, &remainder, method);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalDuration>());
  result = maybe_result.FromJust();

  // Temporal.PlainTime.prototype.since

  // 14. Set result to ! BalanceDuration(0, −result.[[Hours]],
  // −result.[[Minutes]], −result.[[Seconds]], −result.[[Milliseconds]],
  // −result.[[Microseconds]], −result.[[Nanoseconds]], largestUnit).

  // Temporal.PlainTime.prototype.until

  // 13. Set result to ! BalanceDuration(0, result.[[Hours]],
  // result.[[Minutes]], result.[[Seconds]], result.[[Milliseconds]],
  // result.[[Microseconds]], result.[[Nanoseconds]], largestUnit).
  result.days = 0;
  result.hours *= sign;
  result.minutes *= sign;
  result.seconds *= sign;
  result.milliseconds *= sign;
  result.microseconds *= sign;
  result.nanoseconds *= sign;
  Maybe<bool> maybe_balance =
      BalanceDuration(isolate, &result.days, &result.hours, &result.minutes,
                      &result.seconds, &result.milliseconds,
                      &result.microseconds, &result.nanoseconds, largest_unit);
  MAYBE_RETURN(maybe_balance, Handle<JSTemporalDuration>());

  // 15. Return ? CreateTemporalDuration(0, 0, 0, 0, result.[[Hours]],
  // result.[[Minutes]], result.[[Seconds]], result.[[Milliseconds]],
  // result.[[Microseconds]], result.[[Nanoseconds]]).
  return temporal::CreateTemporalDuration(
      isolate, 0, 0, 0, 0, result.hours, result.minutes, result.seconds,
      result.milliseconds, result.microseconds, result.nanoseconds);
}

}  // namespace

// #sec-temporal.plaintime.prototype.until
MaybeHandle<JSTemporalDuration> JSTemporalPlainTime::Until(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> other_obj, Handle<Object> options_obj) {
  return UntilOrSince(isolate, temporal_time, other_obj, options_obj, 1,
                      "Temporal.PlainTime.prototype.until");
}

// #sec-temporal.plaintime.prototype.since
MaybeHandle<JSTemporalDuration> JSTemporalPlainTime::Since(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> other_obj, Handle<Object> options_obj) {
  return UntilOrSince(isolate, temporal_time, other_obj, options_obj, -1,
                      "Temporal.PlainTime.prototype.since");
}

// #sec-temporal.plaintime.prototype.round
MaybeHandle<JSTemporalPlainTime> JSTemporalPlainTime::Round(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> options_obj) {
  const char* method = "Temporal.PlainTime.prototype.round";
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. If options is undefined, then
  if (options_obj->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalPlainTime);
  }
  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalPlainTime);
  // 5. Let smallestUnit be ? ToSmallestTemporalUnit(options, « "year", "month",
  // "week", "day" », undefined).
  Maybe<temporal::Unit> maybe_smallest_unit = ToSmallestTemporalUnit(
      isolate, options,
      std::set<temporal::Unit>({temporal::Unit::kYear, temporal::Unit::kMonth,
                                temporal::Unit::kWeek, temporal::Unit::kDay}),
      temporal::Unit::kNotPresent, method);
  MAYBE_RETURN(maybe_smallest_unit, Handle<JSTemporalPlainTime>());
  temporal::Unit smallest_unit = maybe_smallest_unit.FromJust();

  // 6. If smallestUnit is undefined, throw a RangeError exception.
  if (smallest_unit == temporal::Unit::kNotPresent) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalPlainTime);
  }
  // 7. Let roundingMode be ? ToTemporalRoundingMode(options, "halfExpand").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(
          isolate, options, temporal::RoundingMode::kHalfExpand, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<JSTemporalPlainTime>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();

  double maximum;
  switch (smallest_unit) {
    // 8. If smallestUnit is "hour", then
    case temporal::Unit::kHour:
      // a. Let maximum be 24.
      maximum = 24;
      break;
    // 9. Else if smallestUnit is "minute" or "second", then
    case temporal::Unit::kMinute:
    case temporal::Unit::kSecond:
      // a. Let maximum be 60.
      maximum = 60;
      break;
    // 10. Else,
    default:
      // a. Let maximum be 1000.
      maximum = 1000;
      break;
  }
  // 11. Let roundingIncrement be ? ToTemporalRoundingIncrement(options,
  // maximum, false).
  Maybe<int> maybe_rounding_increment = temporal::ToTemporalRoundingIncrement(
      isolate, options, maximum, true, false, method);
  MAYBE_RETURN(maybe_rounding_increment, Handle<JSTemporalPlainTime>());
  int rounding_increment = maybe_rounding_increment.FromJust();

  // 12. Let result be ! RoundTime(temporalTime.[[ISOHour]],
  // temporalTime.[[ISOMinute]], temporalTime.[[ISOSecond]],
  // temporalTime.[[ISOMillisecond]], temporalTime.[[ISOMicrosecond]],
  // temporalTime.[[ISONanosecond]], roundingIncrement, smallestUnit,
  // roundingMode).
  temporal::DateTimeRecordCommon result = temporal::RoundTime(
      isolate, temporal_time->iso_hour(), temporal_time->iso_minute(),
      temporal_time->iso_second(), temporal_time->iso_millisecond(),
      temporal_time->iso_microsecond(), temporal_time->iso_nanosecond(),
      rounding_increment, smallest_unit, rounding_mode);
  // 13. Return ? CreateTemporalTime(result.[[Hour]], result.[[Minute]],
  // result.[[Second]], result.[[Millisecond]], result.[[Microsecond]],
  // result.[[Nanosecond]]).
  return temporal::CreateTemporalTime(isolate, result.hour, result.minute,
                                      result.second, result.millisecond,
                                      result.microsecond, result.nanosecond);
}

// #sec-temporal.plaintime.prototype.equals
MaybeHandle<Oddball> JSTemporalPlainTime::Equals(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> other_obj) {
  const char* method = "Temporal.PlainTime.prototype.equals";
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. Set other to ? ToTemporalTime(other).
  Handle<JSTemporalPlainTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalTime(isolate, other_obj,
                               temporal::ShowOverflow::kConstrain, method),
      Oddball);
  // 4. If temporalTime.[[ISOHour]] ≠ other.[[ISOHour]], return false.
  if (temporal_time->iso_hour() != other->iso_hour())
    return isolate->factory()->false_value();
  // 5. If temporalTime.[[ISOMinute]] ≠ other.[[ISOMinute]], return false.
  if (temporal_time->iso_minute() != other->iso_minute())
    return isolate->factory()->false_value();
  // 6. If temporalTime.[[ISOSecond]] ≠ other.[[ISOSecond]], return false.
  if (temporal_time->iso_second() != other->iso_second())
    return isolate->factory()->false_value();
  // 7. If temporalTime.[[ISOMillisecond]] ≠ other.[[ISOMillisecond]], return
  // false.
  if (temporal_time->iso_millisecond() != other->iso_millisecond())
    return isolate->factory()->false_value();
  // 8. If temporalTime.[[ISOMicrosecond]] ≠ other.[[ISOMicrosecond]], return
  // false.
  if (temporal_time->iso_microsecond() != other->iso_microsecond())
    return isolate->factory()->false_value();
  // 9. If temporalTime.[[ISONanosecond]] ≠ other.[[ISONanosecond]], return
  // false.
  if (temporal_time->iso_nanosecond() != other->iso_nanosecond())
    return isolate->factory()->false_value();
  // 10. Return true.
  return isolate->factory()->true_value();
}

// #sec-temporal.plaintime.prototype.toplaindatetime
MaybeHandle<JSTemporalPlainDateTime> JSTemporalPlainTime::ToPlainDateTime(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> temporal_date_like) {
  const char* method = "Temporal.PlainTime.prototype.toPlainDateTime";
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. Set temporalDate to ? ToTemporalDate(temporalDate).
  Handle<JSTemporalPlainDate> temporal_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date,
      temporal::ToTemporalDate(isolate, temporal_date_like, method),
      JSTemporalPlainDateTime);
  // 4. Return ? CreateTemporalDateTime(temporalDate.[[ISOYear]],
  // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]],
  // temporalTime.[[ISOHour]], temporalTime.[[ISOMinute]],
  // temporalTime.[[ISOSecond]], temporalTime.[[ISOMillisecond]],
  // temporalTime.[[ISOMicrosecond]], temporalTime.[[ISONanosecond]],
  // temporalDate.[[Calendar]]).
  return temporal::CreateTemporalDateTime(
      isolate, temporal_date->iso_year(), temporal_date->iso_month(),
      temporal_date->iso_day(), temporal_time->iso_hour(),
      temporal_time->iso_minute(), temporal_time->iso_second(),
      temporal_time->iso_millisecond(), temporal_time->iso_microsecond(),
      temporal_time->iso_nanosecond(),
      Handle<JSReceiver>(temporal_date->calendar(), isolate));
}

// #sec-temporal.plaintime.prototype.tozoneddatetime
MaybeHandle<JSTemporalZonedDateTime> JSTemporalPlainTime::ToZonedDateTime(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> item_obj) {
  const char* method = "Temporal.PlainTime.prototype.toZonedDateTime";
  Factory* factory = isolate->factory();
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. If Type(item) is not Object, then
  if (!item_obj->IsJSReceiver()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalZonedDateTime);
  }
  Handle<JSReceiver> item = Handle<JSReceiver>::cast(item_obj);
  // 4. Let temporalDateLike be ? Get(item, "plainDate").
  Handle<Object> temporal_date_like;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_like,
      Object::GetPropertyOrElement(isolate, item, factory->plainDate_string()),
      JSTemporalZonedDateTime);
  // 5. If temporalDateLike is undefined, then
  if (temporal_date_like->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalZonedDateTime);
  }
  // 6. Let temporalDate be ? ToTemporalDate(temporalDateLike).
  Handle<JSTemporalPlainDate> temporal_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date,
      temporal::ToTemporalDate(isolate, temporal_date_like, method),
      JSTemporalZonedDateTime);
  // 7. Let temporalTimeZoneLike be ? Get(item, "timeZone").
  Handle<Object> temporal_time_zone_like;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_time_zone_like,
      Object::GetPropertyOrElement(isolate, item, factory->timeZone_string()),
      JSTemporalZonedDateTime);
  // 8. If temporalTimeZoneLike is undefined, then
  if (temporal_time_zone_like->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalZonedDateTime);
  }
  // 9. Let timeZone be ? ToTemporalTimeZone(temporalTimeZoneLike).
  Handle<JSReceiver> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      temporal::ToTemporalTimeZone(isolate, temporal_time_zone_like, method),
      JSTemporalZonedDateTime);
  // 10. Let temporalDateTime be ?
  // CreateTemporalDateTime(temporalDate.[[ISOYear]], temporalDate.[[ISOMonth]],
  // temporalDate.[[ISODay]], temporalTime.[[ISOHour]],
  // temporalTime.[[ISOMinute]], temporalTime.[[ISOSecond]],
  // temporalTime.[[ISOMillisecond]], temporalTime.[[ISOMicrosecond]],
  // temporalTime.[[ISONanosecond]], temporalDate.[[Calendar]]).
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(temporal_date->calendar(), isolate);
  Handle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_date_time,
      temporal::CreateTemporalDateTime(
          isolate, temporal_date->iso_year(), temporal_date->iso_month(),
          temporal_date->iso_day(), temporal_time->iso_hour(),
          temporal_time->iso_minute(), temporal_time->iso_second(),
          temporal_time->iso_millisecond(), temporal_time->iso_microsecond(),
          temporal_time->iso_nanosecond(), calendar),
      JSTemporalZonedDateTime);
  // 11. Let instant be ? BuiltinTimeZoneGetInstantFor(timeZone,
  // temporalDateTime, "compatible").
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      BuiltinTimeZoneGetInstantFor(isolate, time_zone, temporal_date_time,
                                   temporal::Disambiguation::kCompatible),
      JSTemporalZonedDateTime);
  // 12. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // temporalDate.[[Calendar]]).
  return temporal::CreateTemporalZonedDateTime(
      isolate, Handle<BigInt>(instant->nanoseconds(), isolate), time_zone,
      calendar);
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

// #sec-temporal.plaintime.prototype.tostring
MaybeHandle<String> JSTemporalPlainTime::ToString(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> options_obj) {
  const char* method = "Temporal.PlainTime.prototype.toString";
  // 1. Let temporalTime be the this value.
  // 2. Perform ? RequireInternalSlot(temporalTime,
  // [[InitializedTemporalTime]]).
  // 3. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method), String);
  // 4. Let precision be ? ToSecondsStringPrecision(options).
  temporal::Precision precision;
  double increment;
  temporal::Unit unit;
  Maybe<bool> maybe_precision = ToSecondsStringPrecision(
      isolate, options, &precision, &increment, &unit, method);
  MAYBE_RETURN(maybe_precision, Handle<String>());
  CHECK(maybe_precision.FromJust());

  // 5. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(isolate, options,
                                       temporal::RoundingMode::kTrunc, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<String>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();
  // 6. Let roundResult be ! RoundTime(temporalTime.[[ISOHour]],
  // temporalTime.[[ISOMinute]], temporalTime.[[ISOSecond]],
  // temporalTime.[[ISOMillisecond]], temporalTime.[[ISOMicrosecond]],
  // temporalTime.[[ISONanosecond]], precision.[[Increment]],
  // precision.[[Unit]], roundingMode).
  temporal::DateTimeRecordCommon round_result = temporal::RoundTime(
      isolate, temporal_time->iso_hour(), temporal_time->iso_minute(),
      temporal_time->iso_second(), temporal_time->iso_millisecond(),
      temporal_time->iso_microsecond(), temporal_time->iso_nanosecond(),
      increment, unit, rounding_mode);
  // 7. Return ? TemporalTimeToString(roundResult.[[Hour]],
  // roundResult.[[Minute]], roundResult.[[Second]],
  // roundResult.[[Millisecond]], roundResult.[[Microsecond]],
  // roundResult.[[Nanosecond]], precision.[[Precision]]).

  return temporal::TemporalTimeToString(
      isolate, round_result.hour, round_result.minute, round_result.second,
      round_result.millisecond, round_result.microsecond,
      round_result.nanosecond, precision);
}

// #sec-temporal.plaintime.prototype.tolocalestring
MaybeHandle<String> JSTemporalPlainTime::ToLocaleString(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time,
    Handle<Object> locales, Handle<Object> options) {
  return temporal::TemporalTimeToString(
      isolate, temporal_time->iso_hour(), temporal_time->iso_minute(),
      temporal_time->iso_second(), temporal_time->iso_millisecond(),
      temporal_time->iso_microsecond(), temporal_time->iso_nanosecond(),
      temporal::Precision::kAuto);
}

// #sec-temporal.plaintime.prototype.tojson
MaybeHandle<String> JSTemporalPlainTime::ToJSON(
    Isolate* isolate, Handle<JSTemporalPlainTime> temporal_time) {
  return temporal::TemporalTimeToString(
      isolate, temporal_time->iso_hour(), temporal_time->iso_minute(),
      temporal_time->iso_second(), temporal_time->iso_millisecond(),
      temporal_time->iso_microsecond(), temporal_time->iso_nanosecond(),
      temporal::Precision::kAuto);
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

// #sec-temporal.now.zoneddatetime
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Now(
    Isolate* isolate, Handle<Object> calendar,
    Handle<Object> temporal_time_zone_like) {
  const char* method = "Temporal.Now.zonedDateTime";
  // 1. Return ? SystemZonedDateTime(temporalTimeZoneLike, calendar).
  return temporal::SystemZonedDateTime(isolate, temporal_time_zone_like,
                                       calendar, method);
}

// #sec-temporal.now.zoneddatetimeiso
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::NowISO(
    Isolate* isolate, Handle<Object> temporal_time_zone_like) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.Now.zonedDateTimeISO";
  // 1. Let calendar be ! GetISO8601Calendar().
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             temporal::GetISO8601Calendar(isolate),
                             JSTemporalZonedDateTime);
  // 2. Return ? SystemZonedDateTime(temporalTimeZoneLike, calendar).
  return temporal::SystemZonedDateTime(isolate, temporal_time_zone_like,
                                       calendar, method);
}

// #sec-temporal.zoneddatetime.from
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::From(
    Isolate* isolate, Handle<Object> item, Handle<Object> options_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.ZonedDateTime.from";
  // 1. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalZonedDateTime);

  // 2. If Type(item) is Object and item has an
  // [[InitializedTemporalZonedDateTime]] internal slot, then
  if (item->IsJSTemporalZonedDateTime()) {
    // a. Perform ? ToTemporalOverflow(options).
    Maybe<temporal::ShowOverflow> maybe_overflow =
        temporal::ToTemporalOverflow(isolate, options, method);
    MAYBE_RETURN(maybe_overflow, Handle<JSTemporalZonedDateTime>());

    // b. Perform ? ToTemporalDisambiguation(options).
    Maybe<temporal::Disambiguation> maybe_disambiguation =
        temporal::ToTemporalDisambiguation(isolate, options, method);
    MAYBE_RETURN(maybe_disambiguation, Handle<JSTemporalZonedDateTime>());

    // c. Perform ? ToTemporalOffset(options, "reject").
    Maybe<enum temporal::Offset> maybe_offset = temporal::ToTemporalOffset(
        isolate, options, temporal::Offset::kReject, method);
    MAYBE_RETURN(maybe_offset, Handle<JSTemporalZonedDateTime>());

    // d. Return ? CreateTemporalZonedDateTime(item.[[Nanoseconds]],
    // item.[[TimeZone]], item.[[Calendar]]).
    Handle<JSTemporalZonedDateTime> zoned_date_time =
        Handle<JSTemporalZonedDateTime>::cast(item);
    return temporal::CreateTemporalZonedDateTime(
        isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate),
        Handle<JSReceiver>(zoned_date_time->time_zone(), isolate),
        Handle<JSReceiver>(zoned_date_time->calendar(), isolate));
  }
  // 3. Return ? ToTemporalZonedDateTime(item, options).
  return temporal::ToTemporalZonedDateTime(isolate, item, options, method);
}

// #sec-temporal.zoneddatetime.compare
MaybeHandle<Smi> JSTemporalZonedDateTime::Compare(Isolate* isolate,
                                                  Handle<Object> one_obj,
                                                  Handle<Object> two_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.ZonedDateTime.compare";
  // 1. Set one to ? ToTemporalZonedDateTime(one).
  Handle<JSTemporalZonedDateTime> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one, temporal::ToTemporalZonedDateTime(isolate, one_obj, method),
      Smi);
  // 2. Set two to ? ToTemporalZonedDateTime(two).
  Handle<JSTemporalZonedDateTime> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two, temporal::ToTemporalZonedDateTime(isolate, two_obj, method),
      Smi);
  // 3. Return 𝔽(! CompareEpochNanoseconds(one.[[Nanoseconds]],
  // two.[[Nanoseconds]])).
  return temporal::CompareEpochNanoseconds(
      isolate, Handle<BigInt>(one->nanoseconds(), isolate),
      Handle<BigInt>(two->nanoseconds(), isolate));
}

// #sec-get-temporal.zoneddatetime.prototype.hoursinday
MaybeHandle<Smi> JSTemporalZonedDateTime::HoursInDay(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 4. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      Smi);
  // 5. Let isoCalendar be ! GetISO8601Calendar().
  Handle<JSReceiver> iso_calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, iso_calendar,
                             temporal::GetISO8601Calendar(isolate), Smi);
  // 6. Let temporalDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, isoCalendar).
  Handle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, temporal_date_time,
                             temporal::BuiltinTimeZoneGetPlainDateTimeFor(
                                 isolate, time_zone, instant, iso_calendar),
                             Smi);
  // 7. Let year be temporalDateTime.[[ISOYear]].
  // 8. Let month be temporalDateTime.[[ISOMonth]].
  // 9. Let day be temporalDateTime.[[ISODay]].
  // 10. Let today be ? CreateTemporalDateTime(year, month, day, 0, 0, 0, 0, 0,
  // 0, isoCalendar).
  Handle<JSTemporalPlainDateTime> today;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, today,
      temporal::CreateTemporalDateTime(isolate, temporal_date_time->iso_year(),
                                       temporal_date_time->iso_month(),
                                       temporal_date_time->iso_day(), 0, 0, 0,
                                       0, 0, 0, iso_calendar),
      Smi);
  // 11. Let tomorrowFields be ? AddISODate(year, month, day, 0, 0, 0, 1,
  // "reject").
  int32_t tomorrow_year, tomorrow_month, tomorrow_day;
  Maybe<bool> maybe_tomorrow_fields =
      AddISODate(isolate, temporal_date_time->iso_year(),
                 temporal_date_time->iso_month(), temporal_date_time->iso_day(),
                 0, 0, 0, 1, temporal::ShowOverflow::kReject, &tomorrow_year,
                 &tomorrow_month, &tomorrow_day);
  MAYBE_RETURN(maybe_tomorrow_fields, Handle<Smi>());

  // 12. Let tomorrow be ? CreateTemporalDateTime(tomorrowFields.[[Year]],
  // tomorrowFields.[[Month]], tomorrowFields.[[Day]], 0, 0, 0, 0, 0, 0,
  // isoCalendar).
  Handle<JSTemporalPlainDateTime> tomorrow;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, tomorrow,
                             temporal::CreateTemporalDateTime(
                                 isolate, tomorrow_year, tomorrow_month,
                                 tomorrow_day, 0, 0, 0, 0, 0, 0, iso_calendar),
                             Smi);
  // 13. Let todayInstant be ? BuiltinTimeZoneGetInstantFor(timeZone, today,
  // "compatible").
  Handle<JSTemporalInstant> today_instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, today_instant,
      temporal::BuiltinTimeZoneGetInstantFor(
          isolate, time_zone, today, temporal::Disambiguation::kCompatible),
      Smi);
  // 14. Let tomorrowInstant be ? BuiltinTimeZoneGetInstantFor(timeZone,
  // tomorrow, "compatible").
  Handle<JSTemporalInstant> tomorrow_instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, tomorrow_instant,
      temporal::BuiltinTimeZoneGetInstantFor(
          isolate, time_zone, tomorrow, temporal::Disambiguation::kCompatible),
      Smi);
  // 15. Let diffNs be tomorrowInstant.[[Nanoseconds]] −
  // todayInstant.[[Nanoseconds]].
  Handle<BigInt> diff_ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, diff_ns,
      BigInt::Subtract(isolate,
                       Handle<BigInt>(tomorrow_instant->nanoseconds(), isolate),
                       Handle<BigInt>(today_instant->nanoseconds(), isolate)),
      Smi);
  // 16. Return 𝔽(diffNs / (3.6 × 1012)).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, diff_ns,
      BigInt::Subtract(isolate, diff_ns,
                       BigInt::FromInt64(isolate, 3600000000000)),
      Smi);
  int32_t res = static_cast<int32_t>(diff_ns->AsInt64());
  return Handle<Smi>(Smi::FromInt(res), isolate);
}
// #sec-get-temporal.zoneddatetime.prototype.offsetnanoseconds
MaybeHandle<Object> JSTemporalZonedDateTime::OffsetNanoseconds(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 4. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      Object);
  // 5. Return 𝔽(? GetOffsetNanosecondsFor(timeZone, instant)).
  Maybe<int64_t> maybe_result =
      temporal::GetOffsetNanosecondsFor(isolate, time_zone, instant);
  MAYBE_RETURN(maybe_result, Handle<Object>());
  return isolate->factory()->NewNumberFromInt64(maybe_result.FromJust());
}

// #sec-get-temporal.zoneddatetime.prototype.offset
MaybeHandle<String> JSTemporalZonedDateTime::Offset(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      String);
  // 4. Return ? BuiltinTimeZoneGetOffsetStringFor(zonedDateTime.[[TimeZone]],
  // instant).
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  return temporal::BuiltinTimeZoneGetOffsetStringFor(isolate, time_zone,
                                                     instant);
}
// #sec-temporal.zoneddatetime.prototype.with
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::With(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> temporal_zoned_date_time_like_obj,
    Handle<Object> options_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.ZonedDateTime.prototype.with";
  Factory* factory = isolate->factory();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. If Type(temporalZonedDateTimeLike) is not Object, then
  if (!temporal_zoned_date_time_like_obj->IsJSReceiver()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalZonedDateTime);
  }
  Handle<JSReceiver> temporal_zoned_date_time_like =
      Handle<JSReceiver>::cast(temporal_zoned_date_time_like_obj);
  // 4. Perform ? RejectTemporalCalendarType(temporalZonedDateTimeLike).
  Maybe<bool> maybe_reject = temporal::RejectTemporalCalendarType(
      isolate, temporal_zoned_date_time_like);

  MAYBE_RETURN(maybe_reject, Handle<JSTemporalZonedDateTime>());

  // 5. Let calendarProperty be ? Get(temporalZonedDateTimeLike, "calendar").
  Handle<Object> calendar_property;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_property,
      Object::GetPropertyOrElement(isolate, temporal_zoned_date_time_like,
                                   factory->calendar_string()),
      JSTemporalZonedDateTime);

  // 6. If calendarProperty is not undefined, then
  if (!calendar_property->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalZonedDateTime);
  }
  // 7. Let timeZoneProperty be ? Get(temporalZonedDateTimeLike, "timeZone").
  Handle<Object> time_zone_property;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone_property,
      Object::GetPropertyOrElement(isolate, temporal_zoned_date_time_like,
                                   factory->timeZone_string()),
      JSTemporalZonedDateTime);
  // 8. If timeZoneProperty is not undefined, then
  if (!time_zone_property->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalZonedDateTime);
  }
  // 9. Let calendar be zonedDateTime.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  // 10. Let fieldNames be ? CalendarFields(calendar, « "day", "hour",
  // "microsecond", "millisecond", "minute", "month", "monthCode", "nanosecond",
  // "second", "year" »).
  Handle<FixedArray> field_names = factory->NewFixedArray(10);
  int i = 0;
  field_names->set(i++, *(factory->day_string()));
  field_names->set(i++, *(factory->hour_string()));
  field_names->set(i++, *(factory->microsecond_string()));
  field_names->set(i++, *(factory->millisecond_string()));
  field_names->set(i++, *(factory->minute_string()));
  field_names->set(i++, *(factory->month_string()));
  field_names->set(i++, *(factory->monthCode_string()));
  field_names->set(i++, *(factory->nanosecond_string()));
  field_names->set(i++, *(factory->second_string()));
  field_names->set(i++, *(factory->year_string()));
  CHECK_EQ(i, 10);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, field_names,
      temporal::CalendarFields(isolate, calendar, field_names),
      JSTemporalZonedDateTime);
  // 11. Append "offset" to fieldNames.
  field_names = FixedArray::SetAndGrow(
      isolate, field_names, field_names->length(), factory->offset_string());

  // 12. Let partialZonedDateTime be ?
  // PreparePartialTemporalFields(temporalZonedDateTimeLike, fieldNames).
  Handle<JSObject> partial_zoned_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, partial_zoned_date_time,
      temporal::PreparePartialTemporalFields(
          isolate, temporal_zoned_date_time_like, field_names),
      JSTemporalZonedDateTime);
  // 13. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalZonedDateTime);

  // 14. Let disambiguation be ? ToTemporalDisambiguation(options).
  Maybe<temporal::Disambiguation> maybe_disambiguation =
      temporal::ToTemporalDisambiguation(isolate, options, method);
  MAYBE_RETURN(maybe_disambiguation, Handle<JSTemporalZonedDateTime>());
  temporal::Disambiguation disambiguation = maybe_disambiguation.FromJust();

  // 15. Let offset be ? ToTemporalOffset(options, "prefer").
  Maybe<enum temporal::Offset> maybe_offset = temporal::ToTemporalOffset(
      isolate, options, temporal::Offset::kPrefer, method);
  MAYBE_RETURN(maybe_offset, Handle<JSTemporalZonedDateTime>());
  enum temporal::Offset offset = maybe_offset.FromJust();

  // 16. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 17. Append "timeZone" to fieldNames.
  field_names = FixedArray::SetAndGrow(
      isolate, field_names, field_names->length(), factory->timeZone_string());
  // 18. Let fields be ? PrepareTemporalFields(zonedDateTime, fieldNames, «
  // "timeZone" »).
  Handle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      temporal::PrepareTemporalFields(isolate, zoned_date_time, field_names,
                                      false, true),
      JSTemporalZonedDateTime);
  // 19. Set fields to ? CalendarMergeFields(calendar, fields,
  // partialZonedDateTime).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      temporal::CalendarMergeFields(isolate, calendar, fields,
                                    partial_zoned_date_time),
      JSTemporalZonedDateTime);

  // 20. Set fields to ? PrepareTemporalFields(fields, fieldNames, « "timeZone"
  // »).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, fields,
                             temporal::PrepareTemporalFields(
                                 isolate, fields, field_names, false, true),
                             JSTemporalZonedDateTime);
  // 21. Let offsetString be ? Get(partialZonedDateTime, "offset").
  Handle<Object> offset_string_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, offset_string_obj,
      Object::GetPropertyOrElement(isolate, partial_zoned_date_time,
                                   factory->offset_string()),
      JSTemporalZonedDateTime);
  // 22. If offsetString is not undefined, then
  if (!offset_string_obj->IsUndefined()) {
    // a. Perform ? Set(fields, "offset", offsetString).
    Handle<Object> status;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, status,
        Object::SetProperty(isolate, fields, factory->offset_string(),
                            offset_string_obj, StoreOrigin::kMaybeKeyed,
                            Just(ShouldThrow::kThrowOnError)),
        JSTemporalZonedDateTime);
    // 23. Else,
  } else {
    // a. Set offsetString to ? Get(fields, "offset").
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, offset_string_obj,
        Object::GetPropertyOrElement(isolate, fields, factory->offset_string()),
        JSTemporalZonedDateTime);
    // Set offsetString to ? ToString(offsetString)
    // see https://github.com/tc39/proposal-temporal/pull/1762
    ASSIGN_RETURN_ON_EXCEPTION(isolate, offset_string_obj,
                               Object::ToString(isolate, offset_string_obj),
                               JSTemporalZonedDateTime);
  }
  // 24. Let dateTimeResult be ? InterpretTemporalDateTimeFields(calendar,
  // fields, options).
  Maybe<temporal::DateTimeRecord> maybe_date_time_result =
      temporal::InterpretTemporalDateTimeFields(isolate, calendar, fields,
                                                options, method);
  MAYBE_RETURN(maybe_date_time_result, Handle<JSTemporalZonedDateTime>());
  temporal::DateTimeRecord date_time_result = maybe_date_time_result.FromJust();

  // 25. Let offsetNanoseconds be ? ParseTimeZoneOffsetString(offsetString).
  CHECK(offset_string_obj->IsString());
  Handle<String> offset_string = Handle<String>::cast(offset_string_obj);

  Maybe<int64_t> maybe_offset_nanoseconds =
      temporal::ParseTimeZoneOffsetString(isolate, offset_string);
  MAYBE_RETURN(maybe_offset_nanoseconds, Handle<JSTemporalZonedDateTime>());
  int64_t offset_nanoseconds = maybe_offset_nanoseconds.FromJust();

  // 26. Let epochNanoseconds be ?
  // InterpretISODateTimeOffset(dateTimeResult.[[Year]],
  // dateTimeResult.[[Month]], dateTimeResult.[[Day]], dateTimeResult.[[Hour]],
  // dateTimeResult.[[Minute]], dateTimeResult.[[Second]],
  // dateTimeResult.[[Millisecond]], dateTimeResult.[[Microsecond]],
  // dateTimeResult.[[Nanosecond]], option, offsetNanoseconds, timeZone,
  // disambiguation, offset).
  Handle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, epoch_nanoseconds,
      temporal::InterpretISODateTimeOffset(
          isolate, date_time_result.year, date_time_result.month,
          date_time_result.day, date_time_result.hour, date_time_result.minute,
          date_time_result.second, date_time_result.millisecond,
          date_time_result.microsecond, date_time_result.nanosecond,
          temporal::OffsetBehaviour::kOption, offset_nanoseconds, time_zone,
          disambiguation, offset, method),
      JSTemporalZonedDateTime);

  // 27. Return ? CreateTemporalZonedDateTime(epochNanoseconds, timeZone,
  // calendar).
  return temporal::CreateTemporalZonedDateTime(
      isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate),
      time_zone, calendar);
}

// #sec-temporal.zoneddatetime.prototype.withplaintimne
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::WithPlainTime(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> plain_time_like) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.ZonedDateTime.prototype.withPlainTime";
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. If plainTimeLike is undefined, then
  Handle<JSTemporalPlainTime> plain_time;
  if (plain_time_like->IsUndefined()) {
    // a. Let plainTime be ? CreateTemporalTime(0, 0, 0, 0, 0, 0).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, plain_time,
        temporal::CreateTemporalTime(isolate, 0, 0, 0, 0, 0, 0),
        JSTemporalZonedDateTime);
    // 4. Else,
  } else {
    // a. Let plainTime be ? ToTemporalTime(plainTimeLike).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, plain_time,
        temporal::ToTemporalTime(isolate, plain_time_like, method),
        JSTemporalZonedDateTime);
  }
  // 5. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 6. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      JSTemporalZonedDateTime);
  // 7. Let calendar be zonedDateTime.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  // 8. Let plainDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, calendar).
  Handle<JSTemporalPlainDateTime> plain_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, plain_date_time,
                             temporal::BuiltinTimeZoneGetPlainDateTimeFor(
                                 isolate, time_zone, instant, calendar),
                             JSTemporalZonedDateTime);
  // 9. Let resultPlainDateTime be ?
  // CreateTemporalDateTime(plainDateTime.[[ISOYear]],
  // plainDateTime.[[ISOMonth]], plainDateTime.[[ISODay]],
  // plainTime.[[ISOHour]], plainTime.[[ISOMinute]], plainTime.[[ISOSecond]],
  // plainTime.[[ISOMillisecond]], plainTime.[[ISOMicrosecond]],
  // plainTime.[[ISONanosecond]], calendar).
  Handle<JSTemporalPlainDateTime> result_plain_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result_plain_date_time,
      temporal::CreateTemporalDateTime(
          isolate, plain_date_time->iso_year(), plain_date_time->iso_month(),
          plain_date_time->iso_day(), plain_time->iso_hour(),
          plain_time->iso_minute(), plain_time->iso_second(),
          plain_time->iso_millisecond(), plain_time->iso_microsecond(),
          plain_time->iso_nanosecond(), calendar),
      JSTemporalZonedDateTime);
  // 10. Let instant be ? BuiltinTimeZoneGetInstantFor(timeZone,
  // resultPlainDateTime, "compatible").
  ASSIGN_RETURN_ON_EXCEPTION(isolate, instant,
                             temporal::BuiltinTimeZoneGetInstantFor(
                                 isolate, time_zone, result_plain_date_time,
                                 temporal::Disambiguation::kCompatible),
                             JSTemporalZonedDateTime);
  // 11. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // calendar).
  return temporal::CreateTemporalZonedDateTime(
      isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate),
      time_zone, calendar);
}

// #sec-temporal.zoneddatetime.prototype.withplaindate
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::WithPlainDate(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> plain_date_like) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.ZonedDateTime.prototype.withPlainDate";
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let plainDate be ? ToTemporalDate(plainDateLike).
  Handle<JSTemporalPlainDate> plain_date;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, plain_date,
      temporal::ToTemporalDate(isolate, plain_date_like, method),
      JSTemporalZonedDateTime);
  // 4. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 5. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      JSTemporalZonedDateTime);
  // 6. Let plainDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, zonedDateTime.[[Calendar]]).
  Handle<JSTemporalPlainDateTime> plain_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, plain_date_time,
      temporal::BuiltinTimeZoneGetPlainDateTimeFor(
          isolate, time_zone, instant,
          Handle<JSReceiver>(zoned_date_time->calendar(), isolate)),
      JSTemporalZonedDateTime);
  // 7. Let calendar be ? ConsolidateCalendars(zonedDateTime.[[Calendar]],
  // plainDate.[[Calendar]]).
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      temporal::ConsolidateCalendars(
          isolate, Handle<JSReceiver>(zoned_date_time->calendar(), isolate),
          Handle<JSReceiver>(plain_date->calendar(), isolate)),
      JSTemporalZonedDateTime);
  // 8. Let resultPlainDateTime be ?
  // CreateTemporalDateTime(plainDate.[[ISOYear]], plainDate.[[ISOMonth]],
  // plainDate.[[ISODay]], plainDateTime.[[ISOHour]],
  // plainDateTime.[[ISOMinute]], plainDateTime.[[ISOSecond]],
  // plainDateTime.[[ISOMillisecond]], plainDateTime.[[ISOMicrosecond]],
  // plainDateTime.[[ISONanosecond]], calendar).
  Handle<JSTemporalPlainDateTime> result_plain_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result_plain_date_time,
      temporal::CreateTemporalDateTime(
          isolate, plain_date->iso_year(), plain_date->iso_month(),
          plain_date->iso_day(), plain_date_time->iso_hour(),
          plain_date_time->iso_minute(), plain_date_time->iso_second(),
          plain_date_time->iso_millisecond(),
          plain_date_time->iso_microsecond(), plain_date_time->iso_nanosecond(),
          calendar),
      JSTemporalZonedDateTime);
  // 9. Set instant to ? BuiltinTimeZoneGetInstantFor(timeZone,
  // resultPlainDateTime, "compatible").
  ASSIGN_RETURN_ON_EXCEPTION(isolate, instant,
                             temporal::BuiltinTimeZoneGetInstantFor(
                                 isolate, time_zone, result_plain_date_time,
                                 temporal::Disambiguation::kCompatible),
                             JSTemporalZonedDateTime);
  // 10. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // calendar).
  return temporal::CreateTemporalZonedDateTime(
      isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate),
      time_zone, calendar);
}

// #sec-temporal.zoneddatetime.prototype.withtimezone
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::WithTimeZone(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> time_zone_like) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.ZonedDateTime.prototype.withTimeZone";
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be ? ToTemporalTimeZone(timeZoneLike).
  Handle<JSReceiver> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      temporal::ToTemporalTimeZone(isolate, time_zone_like, method),
      JSTemporalZonedDateTime);

  // 4. Return ? CreateTemporalZonedDateTime(zonedDateTime.[[Nanoseconds]],
  // timeZone, zonedDateTime.[[Calendar]]).
  Handle<BigInt> nanoseconds =
      Handle<BigInt>(zoned_date_time->nanoseconds(), isolate);
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  return temporal::CreateTemporalZonedDateTime(isolate, nanoseconds, time_zone,
                                               calendar);
}
// #sec-temporal.zoneddatetime.prototype.withcalendar
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::WithCalendar(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> calendar_like) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.ZonedDateTime.prototype.withCalendar";
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let calendar be ? ToTemporalCalendar(calendarLike).
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      temporal::ToTemporalCalendar(isolate, calendar_like, method),
      JSTemporalZonedDateTime);

  // 4. Return ? CreateTemporalZonedDateTime(zonedDateTime.[[Nanoseconds]],
  // zonedDateTime.[[TimeZone]], calendar).
  Handle<BigInt> nanoseconds =
      Handle<BigInt>(zoned_date_time->nanoseconds(), isolate);
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  return temporal::CreateTemporalZonedDateTime(isolate, nanoseconds, time_zone,
                                               calendar);
}

namespace {
// #sec-temporal.zoneddatetime.prototype.add
// #sec-temporal.zoneddatetime.prototype.subtract
MaybeHandle<JSTemporalZonedDateTime> AddOrSubtract(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> temporal_duration_like, Handle<Object> options_obj,
    int factor, const char* method) {
  TEMPORAL_ENTER_FUNC
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let duration be ? ToLimitedTemporalDuration(temporalDurationLike, « »).
  Maybe<temporal::DurationRecord> maybe_duration = ToLimitedTemporalDuration(
      isolate, temporal_duration_like, std::set<temporal::Unit>({}), method);
  MAYBE_RETURN(maybe_duration, Handle<JSTemporalZonedDateTime>());
  temporal::DurationRecord duration = maybe_duration.FromJust();

  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalZonedDateTime);

  // 5. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 6. Let calendar be zonedDateTime.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  // 7. Let epochNanoseconds be ?
  // AddZonedDateTime(zonedDateTime.[[Nanoseconds]], timeZone, calendar,
  // −duration.[[Years]], −duration.[[Months]], −duration.[[Weeks]],
  // −duration.[[Days]], −duration.[[Hours]], −duration.[[Minutes]],
  // −duration.[[Seconds]], −duration.[[Milliseconds]],
  // −duration.[[Microseconds]], −duration.[[Nanoseconds]], options).
  Handle<BigInt> nanoseconds =
      Handle<BigInt>(zoned_date_time->nanoseconds(), isolate);
  duration.years *= factor;
  duration.months *= factor;
  duration.weeks *= factor;
  duration.days *= factor;
  duration.hours *= factor;
  duration.minutes *= factor;
  duration.seconds *= factor;
  duration.milliseconds *= factor;
  duration.microseconds *= factor;
  duration.nanoseconds *= factor;
  Handle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, epoch_nanoseconds,
                             AddZonedDateTime(isolate, nanoseconds, time_zone,
                                              calendar, duration, options),
                             JSTemporalZonedDateTime);

  // 8. Return ? CreateTemporalZonedDateTime(epochNanoseconds, timeZone,
  // calendar).
  return temporal::CreateTemporalZonedDateTime(isolate, epoch_nanoseconds,
                                               time_zone, calendar);
}
}  // namespace

// #sec-temporal.zoneddatetime.prototype.add
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Add(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> temporal_duration_like, Handle<Object> options) {
  TEMPORAL_ENTER_FUNC
  return AddOrSubtract(isolate, zoned_date_time, temporal_duration_like,
                       options, 1, "Temporal.ZonedDateTime.prototype.add");
}
// #sec-temporal.zoneddatetime.prototype.subtract
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Subtract(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> temporal_duration_like, Handle<Object> options) {
  TEMPORAL_ENTER_FUNC
  return AddOrSubtract(isolate, zoned_date_time, temporal_duration_like,
                       options, -1,
                       "Temporal.ZonedDateTime.prototype.subtract");
}

namespace {
// #sec-temporal.zoneddatetime.prototype.until
// #sec-temporal.zoneddatetime.prototype.since
MaybeHandle<JSTemporalDuration> UntilOrSince(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> other_obj, Handle<Object> options_obj, int32_t sign,
    const char* method) {
  TEMPORAL_ENTER_FUNC
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Set other to ? ToTemporalZonedDateTime(other).
  Handle<JSTemporalZonedDateTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalZonedDateTime(isolate, other_obj, method),
      JSTemporalDuration);
  // 4. If ? CalendarEquals(zonedDateTime.[[Calendar]], other.[[Calendar]]) is
  // false, then
  Handle<Oddball> eq;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, eq,
      temporal::CalendarEquals(
          isolate, Handle<JSReceiver>(zoned_date_time->calendar(), isolate),
          Handle<JSReceiver>(other->calendar(), isolate)),
      JSTemporalDuration);
  if (eq->IsFalse()) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalDuration);
  }

  // 5. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalDuration);

  // 6. Let smallestUnit be ? ToSmallestTemporalUnit(options, « »,
  // "nanosecond").
  Maybe<temporal::Unit> maybe_smallest_unit =
      ToSmallestTemporalUnit(isolate, options, std::set<temporal::Unit>({}),
                             temporal::Unit::kNanosecond, method);
  MAYBE_RETURN(maybe_smallest_unit, Handle<JSTemporalDuration>());
  temporal::Unit smallest_unit = maybe_smallest_unit.FromJust();

  // 7. Let defaultLargestUnit be ! LargerOfTwoTemporalUnits("hour",
  // smallestUnit).
  temporal::Unit default_largest_unit = temporal::LargerOfTwoTemporalUnits(
      isolate, temporal::Unit::kHour, smallest_unit);

  // 8. Let largestUnit be ? ToLargestTemporalUnit(options, « », "auto",
  // defaultLargestUnit).
  //
  Maybe<temporal::Unit> maybe_largest_unit = temporal::ToLargestTemporalUnit(
      isolate, options, std::set<temporal::Unit>({}), temporal::Unit::kAuto,
      default_largest_unit, method);
  MAYBE_RETURN(maybe_largest_unit, Handle<JSTemporalDuration>());
  temporal::Unit largest_unit = maybe_largest_unit.FromJust();

  // 9. Perform ? ValidateTemporalUnitRange(largestUnit, smallestUnit).
  Maybe<bool> maybe_valid = temporal::ValidateTemporalUnitRange(
      isolate, largest_unit, smallest_unit, method);
  MAYBE_RETURN(maybe_valid, Handle<JSTemporalDuration>());
  CHECK(maybe_valid.FromJust());

  // 10. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(isolate, options,
                                       temporal::RoundingMode::kTrunc, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<JSTemporalDuration>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();

  // 11. Set roundingMode to ! NegateTemporalRoundingMode(roundingMode).
  if (sign == -1) {
    rounding_mode =
        temporal::NegateTemporalRoundingMode(isolate, rounding_mode);
  }

  // 12. Let maximum be !
  // MaximumTemporalDurationRoundingIncrement(smallestUnit).
  double maximum;
  Maybe<bool> maybe_maximum = MaximumTemporalDurationRoundingIncrement(
      isolate, smallest_unit, &maximum);
  MAYBE_RETURN(maybe_maximum, Handle<JSTemporalDuration>());
  bool maximum_is_defined = maybe_maximum.FromJust();

  // 13. Let roundingIncrement be ? ToTemporalRoundingIncrement(options,
  // maximum, false).
  Maybe<int> maybe_rounding_increment = temporal::ToTemporalRoundingIncrement(
      isolate, options, maximum, maximum_is_defined, false, method);
  MAYBE_RETURN(maybe_rounding_increment, Handle<JSTemporalDuration>());
  int rounding_increment = maybe_rounding_increment.FromJust();

  // 14. If largestUnit is not one of "year", "month", "week", or "day", then
  if (largest_unit == temporal::Unit::kYear ||
      largest_unit == temporal::Unit::kMonth ||
      largest_unit == temporal::Unit::kWeek ||
      largest_unit == temporal::Unit::kDay) {
    // a. Let differenceNs be ! DifferenceInstant(zonedDateTime.[[Nanoseconds]],
    // other.[[Nanoseconds]], roundingIncrement, smallestUnit, roundingMode).
    Handle<BigInt> difference_ns;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, difference_ns,
        DifferenceInstant(
            isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate),
            Handle<BigInt>(other->nanoseconds(), isolate), rounding_increment,
            smallest_unit, rounding_mode),
        JSTemporalDuration);

    // b. Let balanceResult be ! BalanceDuration(0, 0, 0, 0, 0, 0, differenceNs,
    // largestUnit).
    temporal::DurationRecord balance_result;
    balance_result.days = balance_result.hours = balance_result.minutes =
        balance_result.seconds = balance_result.milliseconds =
            balance_result.microseconds = 0;
    balance_result.nanoseconds = difference_ns->AsInt64();
    Maybe<bool> maybe_balance = BalanceDuration(
        isolate, &balance_result.days, &balance_result.hours,
        &balance_result.minutes, &balance_result.seconds,
        &balance_result.milliseconds, &balance_result.microseconds,
        &balance_result.nanoseconds, largest_unit);
    MAYBE_RETURN(maybe_balance, Handle<JSTemporalDuration>());
    CHECK(maybe_balance.FromJust());

    // c. Return ? CreateTemporalDuration(0, 0, 0, 0, −balanceResult.[[Hours]],
    // −balanceResult.[[Minutes]], −balanceResult.[[Seconds]],
    // −balanceResult.[[Milliseconds]], −balanceResult.[[Microseconds]],
    // −balanceResult.[[Nanoseconds]]).
    return temporal::CreateTemporalDuration(
        isolate, 0, 0, 0, 0, sign * balance_result.hours,
        sign * balance_result.minutes, sign * balance_result.seconds,
        sign * balance_result.milliseconds, sign * balance_result.microseconds,
        sign * balance_result.nanoseconds);
  }
  // 15. If ? TimeZoneEquals(zonedDateTime.[[TimeZone]], other.[[TimeZone]]) is
  // false, then
  Maybe<bool> maybe_time_zone_equals = temporal::TimeZoneEquals(
      isolate, Handle<JSReceiver>(zoned_date_time->time_zone(), isolate),
      Handle<JSReceiver>(other->time_zone(), isolate));
  MAYBE_RETURN(maybe_time_zone_equals, Handle<JSTemporalDuration>());
  if (!maybe_time_zone_equals.FromJust()) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalDuration);
  }

  // 16. Let untilOptions be ? MergeLargestUnitOption(options, largestUnit).
  Handle<JSObject> until_options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, until_options,
      MergeLargestUnitOption(isolate, options, largest_unit),
      JSTemporalDuration);

  // 17. Let difference be ?
  // DifferenceZonedDateTime(zonedDateTime.[[Nanoseconds]],
  // other.[[Nanoseconds]], zonedDateTime.[[TimeZone]],
  // zonedDateTime.[[Calendar]], largestUnit, untilOptions).
  Maybe<temporal::DurationRecord> maybe_difference = DifferenceZonedDateTime(
      isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate),
      Handle<BigInt>(other->nanoseconds(), isolate),
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate),
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate), largest_unit,
      until_options);
  MAYBE_RETURN(maybe_difference, Handle<JSTemporalDuration>());
  temporal::DurationRecord difference = maybe_difference.FromJust();

  // 18. Let roundResult be ? RoundDuration(difference.[[Years]],
  // difference.[[Months]], difference.[[Weeks]], difference.[[Days]],
  // difference.[[Hours]], difference.[[Minutes]], difference.[[Seconds]],
  // difference.[[Milliseconds]], difference.[[Microseconds]],
  // difference.[[Nanoseconds]], roundingIncrement, smallestUnit, roundingMode,
  // zonedDateTime).
  double remainder;
  Maybe<temporal::DurationRecord> maybe_round_result =
      RoundDuration(isolate, difference, rounding_increment, smallest_unit,
                    rounding_mode, &remainder, method);
  MAYBE_RETURN(maybe_round_result, Handle<JSTemporalDuration>());
  temporal::DurationRecord round_result = maybe_round_result.FromJust();

  // 19. Let result be ? AdjustRoundedDurationDays(roundResult.[[Years]],
  // roundResult.[[Months]], roundResult.[[Weeks]], roundResult.[[Days]],
  // roundResult.[[Hours]], roundResult.[[Minutes]], roundResult.[[Seconds]],
  // roundResult.[[Milliseconds]], roundResult.[[Microseconds]],
  // roundResult.[[Nanoseconds]], roundingIncrement, smallestUnit, roundingMode,
  // zonedDateTime).
  Maybe<temporal::DurationRecord> maybe_result =
      AdjustRoundedDurationDays(isolate, round_result, rounding_increment,
                                smallest_unit, rounding_mode, zoned_date_time);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalDuration>());
  temporal::DurationRecord result = maybe_result.FromJust();

  // 20. Return ? CreateTemporalDuration(−result.[[Years]], −result.[[Months]],
  // −result.[[Weeks]], −result.[[Days]], −result.[[Hours]],
  // −result.[[Minutes]], −result.[[Seconds]], −result.[[Milliseconds]],
  // −result.[[Microseconds]], −result.[[Nanoseconds]]).
  return temporal::CreateTemporalDuration(
      isolate, sign * result.years, sign * result.months, sign * result.weeks,
      sign * result.days, sign * result.hours, sign * result.minutes,
      sign * result.seconds, sign * result.milliseconds,
      sign * result.microseconds, sign * result.nanoseconds);
}

}  // namespace

// #sec-temporal.zoneddatetime.prototype.until
MaybeHandle<JSTemporalDuration> JSTemporalZonedDateTime::Until(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> other_obj, Handle<Object> options_obj) {
  TEMPORAL_ENTER_FUNC
  return UntilOrSince(isolate, zoned_date_time, other_obj, options_obj, 1,
                      "Temporal.ZonedDateTime.prototype.until");
}

// #sec-temporal.zoneddatetime.prototype.since
MaybeHandle<JSTemporalDuration> JSTemporalZonedDateTime::Since(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> other_obj, Handle<Object> options_obj) {
  TEMPORAL_ENTER_FUNC
  return UntilOrSince(isolate, zoned_date_time, other_obj, options_obj, -1,
                      "Temporal.ZonedDateTime.prototype.since");
}

// #sec-temporal.zoneddatetime.prototype.round
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::Round(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> options_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.ZonedDateTime.prototype.round";
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. If options is undefined, then
  if (options_obj->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalZonedDateTime);
  }

  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalZonedDateTime);

  // 5. Let smallestUnit be ? ToSmallestTemporalUnit(options, « "year", "month",
  // "week" », undefined).
  Maybe<temporal::Unit> maybe_smallest_unit = ToSmallestTemporalUnit(
      isolate, options,
      std::set<temporal::Unit>({temporal::Unit::kYear, temporal::Unit::kMonth,
                                temporal::Unit::kWeek}),
      temporal::Unit::kNotPresent, method);
  MAYBE_RETURN(maybe_smallest_unit, Handle<JSTemporalZonedDateTime>());
  temporal::Unit smallest_unit = maybe_smallest_unit.FromJust();

  // 6. If smallestUnit is undefined, throw a RangeError exception.
  if (smallest_unit == temporal::Unit::kNotPresent) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalZonedDateTime);
  }

  // 7. Let roundingMode be ? ToTemporalRoundingMode(options, "halfExpand").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(
          isolate, options, temporal::RoundingMode::kHalfExpand, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<JSTemporalZonedDateTime>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();

  // 8. Let roundingIncrement be ? ToTemporalDateTimeRoundingIncrement(options,
  // smallestUnit).
  Maybe<int> maybe_rounding_increment =
      temporal::ToTemporalDateTimeRoundingIncrement(isolate, options,
                                                    smallest_unit, method);
  MAYBE_RETURN(maybe_rounding_increment, Handle<JSTemporalZonedDateTime>());
  int rounding_increment = maybe_rounding_increment.FromJust();

  // 9. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);

  // 10. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      JSTemporalZonedDateTime);

  // 11. Let calendar be zonedDateTime.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  // 12. Let temporalDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, calendar).
  Handle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, temporal_date_time,
                             temporal::BuiltinTimeZoneGetPlainDateTimeFor(
                                 isolate, time_zone, instant, calendar),
                             JSTemporalZonedDateTime);

  // 13. Let isoCalendar be ! GetISO8601Calendar().
  Handle<JSReceiver> iso_calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, iso_calendar,
                             temporal::GetISO8601Calendar(isolate),
                             JSTemporalZonedDateTime);

  // 14. Let dtStart be ? CreateTemporalDateTime(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]], 0, 0, 0, 0, 0,
  // 0, isoCalendar).
  Handle<JSTemporalPlainDateTime> dt_start;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, dt_start,
      temporal::CreateTemporalDateTime(isolate, temporal_date_time->iso_year(),
                                       temporal_date_time->iso_month(),
                                       temporal_date_time->iso_day(), 0, 0, 0,
                                       0, 0, 0, iso_calendar),
      JSTemporalZonedDateTime);

  // 15. Let instantStart be ? BuiltinTimeZoneGetInstantFor(timeZone, dtStart,
  // "compatible").
  Handle<JSTemporalInstant> instant_start;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant_start,
      temporal::BuiltinTimeZoneGetInstantFor(
          isolate, time_zone, dt_start, temporal::Disambiguation::kCompatible),
      JSTemporalZonedDateTime);

  // 16. Let startNs be instantStart.[[Nanoseconds]].
  Handle<BigInt> start_ns =
      Handle<BigInt>(instant_start->nanoseconds(), isolate);
  // 17. Let endNs be ? AddZonedDateTime(startNs, timeZone,
  // zonedDateTime.[[Calendar]], 0, 0, 0, 1, 0, 0, 0, 0, 0, 0).
  temporal::DurationRecord duration;
  duration.years = duration.months = duration.weeks = duration.hours =
      duration.minutes = duration.seconds = duration.milliseconds =
          duration.microseconds = duration.nanoseconds = 0;
  duration.days = 1;
  Handle<BigInt> end_ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, end_ns,
      AddZonedDateTime(isolate, start_ns, time_zone, calendar, duration),
      JSTemporalZonedDateTime);
  // 18. Let dayLengthNs be ℝ(endNs − startNs).
  Handle<BigInt> day_length_ns;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, day_length_ns,
                             BigInt::Subtract(isolate, end_ns, start_ns),
                             JSTemporalZonedDateTime);
  // 19. If dayLengthNs is 0, then
  if (!day_length_ns->ToBoolean()) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalZonedDateTime);
  }
  // 20. Let roundResult be ! RoundISODateTime(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]],
  // temporalDateTime.[[ISOHour]], temporalDateTime.[[ISOMinute]],
  // temporalDateTime.[[ISOSecond]], temporalDateTime.[[ISOMillisecond]],
  // temporalDateTime.[[ISOMicrosecond]], temporalDateTime.[[ISONanosecond]],
  // roundingIncrement, smallestUnit, roundingMode, dayLengthNs).
  temporal::DateTimeRecordCommon round_result = RoundISODateTime(
      isolate, temporal_date_time->iso_year(), temporal_date_time->iso_month(),
      temporal_date_time->iso_day(), temporal_date_time->iso_hour(),
      temporal_date_time->iso_minute(), temporal_date_time->iso_second(),
      temporal_date_time->iso_millisecond(),
      temporal_date_time->iso_microsecond(),
      temporal_date_time->iso_nanosecond(), rounding_increment, smallest_unit,
      rounding_mode, day_length_ns->AsInt64());

  // 21. Let offsetNanoseconds be ? GetOffsetNanosecondsFor(timeZone, instant).
  Maybe<int64_t> maybe_offset_nanoseconds =
      temporal::GetOffsetNanosecondsFor(isolate, time_zone, instant);
  MAYBE_RETURN(maybe_offset_nanoseconds, Handle<JSTemporalZonedDateTime>());
  int64_t offset_nanoseconds = maybe_offset_nanoseconds.FromJust();

  // 22. Let epochNanoseconds be ?
  // InterpretISODateTimeOffset(roundResult.[[Year]], roundResult.[[Month]],
  // roundResult.[[Day]], roundResult.[[Hour]], roundResult.[[Minute]],
  // roundResult.[[Second]], roundResult.[[Millisecond]],
  // roundResult.[[Microsecond]], roundResult.[[Nanosecond]], offsetNanoseconds,
  // timeZone, "compatible", "prefer").
  Handle<BigInt> epoch_nanoseconds;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, epoch_nanoseconds,
      InterpretISODateTimeOffset(
          isolate, round_result.year, round_result.month, round_result.day,
          round_result.hour, round_result.minute, round_result.second,
          round_result.millisecond, round_result.microsecond,
          round_result.nanosecond, temporal::OffsetBehaviour::kOption,
          offset_nanoseconds, time_zone, temporal::Disambiguation::kCompatible,
          temporal::Offset::kPrefer, method),
      JSTemporalZonedDateTime);

  // 23. Return ? CreateTemporalZonedDateTime(epochNanoseconds, timeZone,
  // calendar).
  return temporal::CreateTemporalZonedDateTime(isolate, epoch_nanoseconds,
                                               time_zone, calendar);
}

// #sec-temporal.zoneddatetime.prototype.equals
MaybeHandle<Oddball> JSTemporalZonedDateTime::Equals(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> other_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.ZonedDateTime.prototype.equals";
  Factory* factory = isolate->factory();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Set other to ? ToTemporalZonedDateTime(other).
  Handle<JSTemporalZonedDateTime> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other,
      temporal::ToTemporalZonedDateTime(isolate, other_obj, method), Oddball);
  // 4. If zonedDateTime.[[Nanoseconds]] ≠ other.[[Nanoseconds]], return false.
  Maybe<bool> maybe_nanoseconds_equals = BigInt::Equals(
      isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate),
      Handle<BigInt>(other->nanoseconds(), isolate));
  MAYBE_RETURN(maybe_nanoseconds_equals, Handle<Oddball>());
  if (!maybe_nanoseconds_equals.FromJust()) {
    return factory->false_value();
  }
  // 5. If ? TimeZoneEquals(zonedDateTime.[[TimeZone]], other.[[TimeZone]]) is
  // false, return false.
  Maybe<bool> maybe_time_zone_equals = temporal::TimeZoneEquals(
      isolate, Handle<JSReceiver>(zoned_date_time->time_zone(), isolate),
      Handle<JSReceiver>(other->time_zone(), isolate));
  MAYBE_RETURN(maybe_time_zone_equals, Handle<Oddball>());
  if (!maybe_time_zone_equals.FromJust()) {
    return factory->false_value();
  }
  // 6. Return ? CalendarEquals(zonedDateTime.[[Calendar]], other.[[Calendar]]).
  return temporal::CalendarEquals(
      isolate, Handle<JSReceiver>(zoned_date_time->calendar(), isolate),
      Handle<JSReceiver>(other->calendar(), isolate));
}
// #sec-temporal.zoneddatetime.prototype.startofday
MaybeHandle<JSTemporalZonedDateTime> JSTemporalZonedDateTime::StartOfDay(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 4. Let calendar be zonedDateTime.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  // 5. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      JSTemporalZonedDateTime);
  // 6. Let temporalDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, calendar).
  Handle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, temporal_date_time,
                             temporal::BuiltinTimeZoneGetPlainDateTimeFor(
                                 isolate, time_zone, instant, calendar),
                             JSTemporalZonedDateTime);
  // 7. Let startDateTime be ?
  // CreateTemporalDateTime(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]], 0, 0, 0, 0, 0,
  // 0, calendar).
  Handle<JSTemporalPlainDateTime> start_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, start_date_time,
      temporal::CreateTemporalDateTime(isolate, temporal_date_time->iso_year(),
                                       temporal_date_time->iso_month(),
                                       temporal_date_time->iso_day(), 0, 0, 0,
                                       0, 0, 0, calendar),
      JSTemporalZonedDateTime);
  // 8. Let startInstant be ? BuiltinTimeZoneGetInstantFor(timeZone,
  // startDateTime, "compatible").
  Handle<JSTemporalInstant> start_instant;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, start_instant,
                             temporal::BuiltinTimeZoneGetInstantFor(
                                 isolate, time_zone, start_date_time,
                                 temporal::Disambiguation::kCompatible),
                             JSTemporalZonedDateTime);
  // 9. Return ? CreateTemporalZonedDateTime(startInstant.[[Nanoseconds]],
  // timeZone, calendar).
  return temporal::CreateTemporalZonedDateTime(
      isolate, Handle<BigInt>(start_instant->nanoseconds(), isolate), time_zone,
      calendar);
}

// #sec-temporal.zoneddatetime.prototype.toinstant
MaybeHandle<JSTemporalInstant> JSTemporalZonedDateTime::ToInstant(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Return ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  return temporal::CreateTemporalInstant(
      isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate));
}
// #sec-temporal.zoneddatetime.prototype.toplaindate
MaybeHandle<JSTemporalPlainDate> JSTemporalZonedDateTime::ToPlainDate(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 4. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      JSTemporalPlainDate);
  // 5. Let calendar be zonedDateTime.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  // 6. Let temporalDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, calendar).
  Handle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, temporal_date_time,
                             temporal::BuiltinTimeZoneGetPlainDateTimeFor(
                                 isolate, time_zone, instant, calendar),
                             JSTemporalPlainDate);
  // 7. Return ? CreateTemporalDate(temporalDateTime.[[ISOYear]],
  // temporalDateTime.[[ISOMonth]], temporalDateTime.[[ISODay]], calendar).
  return temporal::CreateTemporalDate(isolate, temporal_date_time->iso_year(),
                                      temporal_date_time->iso_month(),
                                      temporal_date_time->iso_day(), calendar);
}
// #sec-temporal.zoneddatetime.prototype.toplaintime
MaybeHandle<JSTemporalPlainTime> JSTemporalZonedDateTime::ToPlainTime(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 4. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      JSTemporalPlainTime);
  // 5. Let calendar be zonedDateTime.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  // 6. Let temporalDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, calendar).
  Handle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, temporal_date_time,
                             temporal::BuiltinTimeZoneGetPlainDateTimeFor(
                                 isolate, time_zone, instant, calendar),
                             JSTemporalPlainTime);
  // 7. Return ?  CreateTemporalTime(temporalDateTime.[[ISOHour]],
  // temporalDateTime.[[ISOMinute]], temporalDateTime.[[ISOSecond]],
  // temporalDateTime.[[ISOMillisecond]], temporalDateTime.[[ISOMicrosecond]],
  // temporalDateTime.[[ISONanosecond]]).
  return temporal::CreateTemporalTime(
      isolate, temporal_date_time->iso_hour(), temporal_date_time->iso_minute(),
      temporal_date_time->iso_second(), temporal_date_time->iso_millisecond(),
      temporal_date_time->iso_microsecond(),
      temporal_date_time->iso_nanosecond());
}
// #sec-temporal.zoneddatetime.prototype.toplaindatetime
MaybeHandle<JSTemporalPlainDateTime> JSTemporalZonedDateTime::ToPlainDateTime(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 4. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      JSTemporalPlainDateTime);
  // 5. Let calendar be zonedDateTime.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  // 6. Return ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone, instant,
  // calendar).
  return temporal::BuiltinTimeZoneGetPlainDateTimeFor(isolate, time_zone,
                                                      instant, calendar);
}
// #sec-temporal.zoneddatetime.prototype.toplainyearmonth
MaybeHandle<JSTemporalPlainYearMonth> JSTemporalZonedDateTime::ToPlainYearMonth(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC
  Factory* factory = isolate->factory();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 4. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      JSTemporalPlainYearMonth);
  // 5. Let calendar be zonedDateTime.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  // 6. Let temporalDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, calendar).
  Handle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, temporal_date_time,
                             temporal::BuiltinTimeZoneGetPlainDateTimeFor(
                                 isolate, time_zone, instant, calendar),
                             JSTemporalPlainYearMonth);
  // 7. Let fieldNames be ? CalendarFields(calendar, « "monthCode", "year" »).
  Handle<FixedArray> field_names = factory->NewFixedArray(2);
  field_names->set(0, *(factory->monthCode_string()));
  field_names->set(1, *(factory->year_string()));
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, field_names,
      temporal::CalendarFields(isolate, calendar, field_names),
      JSTemporalPlainYearMonth);
  // 8. Let fields be ? PrepareTemporalFields(temporalDateTime, fieldNames, «»).
  Handle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      temporal::PrepareTemporalFields(isolate, temporal_date_time, field_names,
                                      false, false),
      JSTemporalPlainYearMonth);
  // 9. Return ? YearMonthFromFields(calendar, fields).
  return temporal::YearMonthFromFields(isolate, calendar, fields,
                                       factory->undefined_value());
}
// #sec-temporal.zoneddatetime.prototype.toplainmonthday
MaybeHandle<JSTemporalPlainMonthDay> JSTemporalZonedDateTime::ToPlainMonthDay(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC
  Factory* factory = isolate->factory();
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Let timeZone be zonedDateTime.[[TimeZone]].
  Handle<JSReceiver> time_zone =
      Handle<JSReceiver>(zoned_date_time->time_zone(), isolate);
  // 4. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  Handle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, instant,
      temporal::CreateTemporalInstant(
          isolate, Handle<BigInt>(zoned_date_time->nanoseconds(), isolate)),
      JSTemporalPlainMonthDay);
  // 5. Let calendar be zonedDateTime.[[Calendar]].
  Handle<JSReceiver> calendar =
      Handle<JSReceiver>(zoned_date_time->calendar(), isolate);
  // 6. Let temporalDateTime be ? BuiltinTimeZoneGetPlainDateTimeFor(timeZone,
  // instant, calendar).
  Handle<JSTemporalPlainDateTime> temporal_date_time;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, temporal_date_time,
                             temporal::BuiltinTimeZoneGetPlainDateTimeFor(
                                 isolate, time_zone, instant, calendar),
                             JSTemporalPlainMonthDay);
  // 7. Let fieldNames be ? CalendarFields(calendar, « "day", "monthCode" »).
  Handle<FixedArray> field_names = factory->NewFixedArray(2);
  field_names->set(0, *(factory->day_string()));
  field_names->set(1, *(factory->monthCode_string()));
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, field_names,
      temporal::CalendarFields(isolate, calendar, field_names),
      JSTemporalPlainMonthDay);
  // 8. Let fields be ? PrepareTemporalFields(temporalDateTime, fieldNames, «»).
  Handle<JSReceiver> fields;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, fields,
      temporal::PrepareTemporalFields(isolate, temporal_date_time, field_names,
                                      false, false),
      JSTemporalPlainMonthDay);
  // 9. Return ? MonthDayFromFields(calendar, fields).
  return temporal::MonthDayFromFields(isolate, calendar, fields,
                                      factory->undefined_value());
}

// #sec-temporal.zoneddatetime.prototype.getisofields
MaybeHandle<JSReceiver> JSTemporalZonedDateTime::GetISOFields(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC
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
  ADD_INT_FIELD(fields, isoNanosecond, iso_nanosecond, date_time)
  ADD_INT_FIELD(fields, isoSecond, iso_second, date_time)
  ADD_INT_FIELD(fields, isoYear, iso_year, date_time)
  ADD_STRING_FIELD(fields, offset, offset)
  ADD_STRING_FIELD(fields, timeZone, time_zone)
  // 21. Return fields.
  return fields;
}

// #sec-temporal.zoneddatetime.prototype.tostring
MaybeHandle<String> JSTemporalZonedDateTime::ToString(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> options_obj) {
  TEMPORAL_ENTER_FUNC
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  const char* method = "Temporal.ZonedDateTime.prototype.toString";
  // 3. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method), String);
  // 4. Let precision be ? ToSecondsStringPrecision(options).
  temporal::Precision precision;
  double increment;
  temporal::Unit unit;
  Maybe<bool> maybe_precision = ToSecondsStringPrecision(
      isolate, options, &precision, &increment, &unit, method);
  MAYBE_RETURN(maybe_precision, Handle<String>());
  CHECK(maybe_precision.FromJust());

  // 5. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(isolate, options,
                                       temporal::RoundingMode::kTrunc, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<String>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();

  // 6. Let showCalendar be ? ToShowCalendarOption(options).
  Maybe<temporal::ShowCalendar> maybe_show_calendar =
      temporal::ToShowCalendarOption(isolate, options, method);
  MAYBE_RETURN(maybe_show_calendar, Handle<String>());
  temporal::ShowCalendar show_calendar = maybe_show_calendar.FromJust();

  // 7. Let showTimeZone be ? ToShowTimeZoneNameOption(options).
  Maybe<temporal::ShowTimeZone> maybe_show_time_zone =
      temporal::ToShowTimeZoneNameOption(isolate, options, method);
  MAYBE_RETURN(maybe_show_time_zone, Handle<String>());
  temporal::ShowTimeZone show_time_zone = maybe_show_time_zone.FromJust();
  // 8. Let showOffset be ? ToShowOffsetOption(options).
  Maybe<temporal::ShowOffset> maybe_show_offset =
      temporal::ToShowOffsetOption(isolate, options, method);
  MAYBE_RETURN(maybe_show_offset, Handle<String>());
  temporal::ShowOffset show_offset = maybe_show_offset.FromJust();
  // 9. Return ? TemporalZonedDateTimeToString(zonedDateTime,
  // precision.[[Precision]], showCalendar, showTimeZone, showOffset,
  // precision.[[Increment]], precision.[[Unit]], roundingMode).
  return temporal::TemporalZonedDateTimeToString(
      isolate, zoned_date_time, precision, show_calendar, show_time_zone,
      show_offset, increment, unit, rounding_mode);
}

// #sec-temporal.zoneddatetime.prototype.tolocalestring
MaybeHandle<String> JSTemporalZonedDateTime::ToLocaleString(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time,
    Handle<Object> locales, Handle<Object> options) {
  TEMPORAL_ENTER_FUNC
  return ToJSON(isolate, zoned_date_time);
}

// #sec-temporal.zoneddatetime.prototype.tojson
MaybeHandle<String> JSTemporalZonedDateTime::ToJSON(
    Isolate* isolate, Handle<JSTemporalZonedDateTime> zoned_date_time) {
  TEMPORAL_ENTER_FUNC
  // 1. Let zonedDateTime be the this value.
  // 2. Perform ? RequireInternalSlot(zonedDateTime,
  // [[InitializedTemporalZonedDateTime]]).
  // 3. Return ? TemporalZonedDateTimeToString(zonedDateTime, "auto", "auto",
  // "auto", "auto").
  return TemporalZonedDateTimeToString(
      isolate, zoned_date_time, temporal::Precision::kAuto,
      temporal::ShowCalendar::kAuto, temporal::ShowTimeZone::kAuto,
      temporal::ShowOffset::kAuto);
}

// #sec-temporal.now.instant
MaybeHandle<JSTemporalInstant> JSTemporalInstant::Now(Isolate* isolate) {
  TEMPORAL_ENTER_FUNC
  return temporal::SystemInstant(isolate);
}

// #sec-temporal.instant
MaybeHandle<JSTemporalInstant> JSTemporalInstant::Constructor(
    Isolate* isolate, Handle<JSFunction> target, Handle<HeapObject> new_target,
    Handle<Object> epoch_nanoseconds_obj) {
  TEMPORAL_ENTER_FUNC
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

namespace {

MaybeHandle<JSTemporalInstant> ScaleNumberToNanosecondsVerifyAndMake(
    Isolate* isolate, Handle<BigInt> bigint, uint32_t scale) {
  TEMPORAL_ENTER_FUNC
  // 2. Let epochNanoseconds be epochXseconds × scaleℤ.
  Handle<BigInt> epoch_nanoseconds;
  if (scale == 1) {
    epoch_nanoseconds = bigint;
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, epoch_nanoseconds,
        BigInt::Multiply(isolate, BigInt::FromUint64(isolate, scale), bigint),
        JSTemporalInstant);
  }
  // 3. If ! IsValidEpochNanoseconds(epochNanoseconds) is false, throw a
  // RangeError exception.
  if (!temporal::IsValidEpochNanoseconds(isolate, epoch_nanoseconds)) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalInstant);
  }
  return temporal::CreateTemporalInstant(isolate, epoch_nanoseconds);
}

// The logic in Temporal.Instant.fromEpochSeconds and fromEpochMilliseconds,
// the same except a
// scaling factor, code all of them into the follow function.
MaybeHandle<JSTemporalInstant> ScaleNumberToNanosecondsVerifyAndMake(
    Isolate* isolate, Handle<Object> epoch_Xseconds_obj, uint32_t scale) {
  TEMPORAL_ENTER_FUNC
  // 1. Set epochXseconds to ? ToNumber(epochXseconds).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, epoch_Xseconds_obj,
                             Object::ToNumber(isolate, epoch_Xseconds_obj),
                             JSTemporalInstant);
  // 2. Set epochMilliseconds to ? NumberToBigInt(epochMilliseconds).
  Handle<BigInt> epoch_Xseconds;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, epoch_Xseconds,
                             BigInt::FromNumber(isolate, epoch_Xseconds_obj),
                             JSTemporalInstant);
  return ScaleNumberToNanosecondsVerifyAndMake(isolate, epoch_Xseconds, scale);
}

// The logic in fromEpochMicroseconds and fromEpochNanoseconds are the same
// except a scaling factor, code all of them into the follow function.
MaybeHandle<JSTemporalInstant> ScaleToNanosecondsVerifyAndMake(
    Isolate* isolate, Handle<Object> epoch_Xseconds_obj, uint32_t scale) {
  TEMPORAL_ENTER_FUNC
  // 1. Set epochMicroseconds to ? ToBigInt(epochMicroseconds).
  Handle<BigInt> epoch_Xseconds;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, epoch_Xseconds,
                             BigInt::FromObject(isolate, epoch_Xseconds_obj),
                             JSTemporalInstant);
  return ScaleNumberToNanosecondsVerifyAndMake(isolate, epoch_Xseconds, scale);
}
}  // namespace

// #sec-temporal.instant.from
MaybeHandle<JSTemporalInstant> JSTemporalInstant::From(Isolate* isolate,
                                                       Handle<Object> item) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.Instant.from";
  //  1. If Type(item) is Object and item has an [[InitializedTemporalInstant]]
  //  internal slot, then
  if (item->IsJSTemporalInstant()) {
    // a. Return ? CreateTemporalInstant(item.[[Nanoseconds]]).
    Handle<BigInt> nanoseconds =
        Handle<BigInt>(JSTemporalInstant::cast(*item).nanoseconds(), isolate);
    return temporal::CreateTemporalInstant(isolate, nanoseconds);
  }
  // 2. Return ? ToTemporalInstant(item).
  return temporal::ToTemporalInstant(isolate, item, method);
}

// #sec-temporal.instant.fromepochseconds
MaybeHandle<JSTemporalInstant> JSTemporalInstant::FromEpochSeconds(
    Isolate* isolate, Handle<Object> epoch_seconds) {
  TEMPORAL_ENTER_FUNC
  return ScaleNumberToNanosecondsVerifyAndMake(isolate, epoch_seconds,
                                               1000000000);
}
// #sec-temporal.instant.fromepochmilliseconds
MaybeHandle<JSTemporalInstant> JSTemporalInstant::FromEpochMilliseconds(
    Isolate* isolate, Handle<Object> epoch_milliseconds) {
  TEMPORAL_ENTER_FUNC
  return ScaleNumberToNanosecondsVerifyAndMake(isolate, epoch_milliseconds,
                                               1000000);
}

// #sec-temporal.instant.fromepochmicroseconds
MaybeHandle<JSTemporalInstant> JSTemporalInstant::FromEpochMicroseconds(
    Isolate* isolate, Handle<Object> epoch_microseconds) {
  TEMPORAL_ENTER_FUNC
  return ScaleToNanosecondsVerifyAndMake(isolate, epoch_microseconds, 1000);
}
// #sec-temporal.instant.fromepochnanoeconds
MaybeHandle<JSTemporalInstant> JSTemporalInstant::FromEpochNanoseconds(
    Isolate* isolate, Handle<Object> epoch_nanoseconds) {
  TEMPORAL_ENTER_FUNC
  return ScaleToNanosecondsVerifyAndMake(isolate, epoch_nanoseconds, 1);
}

// #sec-temporal.instant.compare
MaybeHandle<Smi> JSTemporalInstant::Compare(Isolate* isolate,
                                            Handle<Object> one_obj,
                                            Handle<Object> two_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.Instant.compare";
  // 1. Set one to ? ToTemporalInstant(one).
  Handle<JSTemporalInstant> one;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, one, temporal::ToTemporalInstant(isolate, one_obj, method), Smi);
  // 2. Set two to ? ToTemporalInstant(two).
  Handle<JSTemporalInstant> two;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, two, temporal::ToTemporalInstant(isolate, two_obj, method), Smi);
  // 3. Return 𝔽(! CompareEpochNanoseconds(one.[[Nanoseconds]],
  // two.[[Nanoseconds]])).
  return temporal::CompareEpochNanoseconds(
      isolate, Handle<BigInt>(one->nanoseconds(), isolate),
      Handle<BigInt>(two->nanoseconds(), isolate));
}

namespace {
// #sec-temporal.instant.prototype.add
// #sec-temporal.instant.prototype.subtract
MaybeHandle<JSTemporalInstant> AddOrSubtract(
    Isolate* isolate, Handle<JSTemporalInstant> handle,
    Handle<Object> temporal_duration_like, int factor, const char* method) {
  TEMPORAL_ENTER_FUNC
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. Let duration be ? ToLimitedTemporalDuration(temporalDurationLike, «
  // "years", "months", "weeks", "days" »).
  Maybe<temporal::DurationRecord> maybe_duration = ToLimitedTemporalDuration(
      isolate, temporal_duration_like,
      std::set<temporal::Unit>({temporal::Unit::kYear, temporal::Unit::kMonth,
                                temporal::Unit::kWeek, temporal::Unit::kDay}),
      method);
  MAYBE_RETURN(maybe_duration, Handle<JSTemporalInstant>());
  temporal::DurationRecord duration = maybe_duration.FromJust();
  // 4. Let ns be ? AddInstant(instant.[[EpochNanoseconds]], duration.[[Hours]],
  // duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]],
  // duration.[[Microseconds]], duration.[[Nanoseconds]]).
  Handle<BigInt> ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, ns,
      temporal::AddInstant(
          isolate, Handle<BigInt>(handle->nanoseconds(), isolate),
          factor * duration.hours, factor * duration.minutes,
          factor * duration.seconds, factor * duration.milliseconds,
          factor * duration.microseconds, factor * duration.nanoseconds),
      JSTemporalInstant);
  // 5. Return ! CreateTemporalInstant(ns).
  return temporal::CreateTemporalInstant(isolate, ns);
}
}  // namespace

// #sec-temporal.instant.prototype.add
MaybeHandle<JSTemporalInstant> JSTemporalInstant::Add(
    Isolate* isolate, Handle<JSTemporalInstant> handle,
    Handle<Object> temporal_duration_like) {
  TEMPORAL_ENTER_FUNC
  return AddOrSubtract(isolate, handle, temporal_duration_like, 1,
                       "Temporal.Instant.prototype.add");
}

// #sec-temporal.instant.prototype.subtract
MaybeHandle<JSTemporalInstant> JSTemporalInstant::Subtract(
    Isolate* isolate, Handle<JSTemporalInstant> handle,
    Handle<Object> temporal_duration_like) {
  TEMPORAL_ENTER_FUNC
  return AddOrSubtract(isolate, handle, temporal_duration_like, -1,
                       "Temporal.Instant.prototype.subtract");
}

namespace {
// The common routine for until and since. The only difference is the order of
// instant.[[Nanoseconds]] and other.[[Nanoseconds]] passing in to
// DifferenceInstant
MaybeHandle<JSTemporalDuration> UntilOrSince(Isolate* isolate,
                                             Handle<JSTemporalInstant> handle,
                                             Handle<Object> other_obj,
                                             Handle<Object> options_obj,
                                             bool until, const char* method) {
  TEMPORAL_ENTER_FUNC
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. Set other to ? ToTemporalInstant(other).
  Handle<JSTemporalInstant> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, temporal::ToTemporalInstant(isolate, other_obj, method),
      JSTemporalDuration);
  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalDuration);
  // 5. Let smallestUnit be ? ToSmallestTemporalUnit(options, « "year", "month",
  // "week", "day" », "nanosecond").
  Maybe<temporal::Unit> maybe_smallest_unit = ToSmallestTemporalUnit(
      isolate, options,
      std::set<temporal::Unit>({temporal::Unit::kYear, temporal::Unit::kMonth,
                                temporal::Unit::kWeek, temporal::Unit::kDay}),
      temporal::Unit::kNanosecond, method);
  MAYBE_RETURN(maybe_smallest_unit, Handle<JSTemporalDuration>());
  temporal::Unit smallest_unit = maybe_smallest_unit.FromJust();
  // 6. Let defaultLargestUnit be ! LargerOfTwoTemporalUnits("second",
  // smallestUnit).
  temporal::Unit default_largest_unit =
      LargerOfTwoTemporalUnits(isolate, temporal::Unit::kSecond, smallest_unit);
  // 7. Let largestUnit be ? ToLargestTemporalUnit(options, « "year", "month",
  // "week", "day" », "auto", defaultLargestUnit).
  Maybe<temporal::Unit> maybe_largest_unit = ToLargestTemporalUnit(
      isolate, options,
      std::set<temporal::Unit>({temporal::Unit::kYear, temporal::Unit::kMonth,
                                temporal::Unit::kWeek, temporal::Unit::kDay}),
      temporal::Unit::kAuto, default_largest_unit, method);
  MAYBE_RETURN(maybe_largest_unit, Handle<JSTemporalDuration>());
  temporal::Unit largest_unit = maybe_largest_unit.FromJust();
  // 8. Perform ? ValidateTemporalUnitRange(largestUnit, smallestUnit).
  Maybe<bool> maybe_valid =
      ValidateTemporalUnitRange(isolate, largest_unit, smallest_unit, method);
  MAYBE_RETURN(maybe_valid, Handle<JSTemporalDuration>());
  CHECK(maybe_valid.FromJust());
  // 9. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(isolate, options,
                                       temporal::RoundingMode::kTrunc, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<JSTemporalDuration>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();

  // 10. Let maximum be !
  // MaximumTemporalDurationRoundingIncrement(smallestUnit).
  double maximum;
  Maybe<bool> maybe_maximum = MaximumTemporalDurationRoundingIncrement(
      isolate, smallest_unit, &maximum);
  MAYBE_RETURN(maybe_maximum, Handle<JSTemporalDuration>());
  bool maximum_is_defined = maybe_maximum.FromJust();
  // 11. Let roundingIncrement be ? ToTemporalRoundingIncrement(options,
  // maximum, false).
  Maybe<int> maybe_rounding_increment = temporal::ToTemporalRoundingIncrement(
      isolate, options, maximum, maximum_is_defined, false, method);
  MAYBE_RETURN(maybe_rounding_increment, Handle<JSTemporalDuration>());
  int rounding_increment = maybe_rounding_increment.FromJust();
  // 12. Let roundedNs be ! DifferenceInstant(instant.[[Nanoseconds]],
  // other.[[Nanoseconds]], roundingIncrement, smallestUnit, roundingMode).
  Handle<BigInt> first = Handle<BigInt>(
      until ? handle->nanoseconds() : other->nanoseconds(), isolate);
  Handle<BigInt> second = Handle<BigInt>(
      until ? other->nanoseconds() : handle->nanoseconds(), isolate);
  Handle<BigInt> rounded_ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, rounded_ns,
      DifferenceInstant(isolate, first, second, rounding_increment,
                        smallest_unit, rounding_mode),
      JSTemporalDuration);
  // 13. Let result be ! BalanceDuration(0, 0, 0, 0, 0, 0, roundedNs,
  // largestUnit).
  double result_days = 0;
  double result_hours = 0;
  double result_minutes = 0;
  double result_seconds = 0;
  double result_milliseconds = 0;
  double result_microseconds = 0;
  double result_nanoseconds = rounded_ns->AsInt64();
  Maybe<bool> maybe_result =
      BalanceDuration(isolate, &result_days, &result_hours, &result_minutes,
                      &result_seconds, &result_milliseconds,
                      &result_microseconds, &result_nanoseconds, largest_unit);
  MAYBE_RETURN(maybe_result, Handle<JSTemporalDuration>());
  // 14. Return ? CreateTemporalDuration(0, 0, 0, 0, result.[[Hours]],
  // result.[[Minutes]], result.[[Seconds]], result.[[Milliseconds]],
  // result.[[Microseconds]], result.[[Nanoseconds]]).
  return temporal::CreateTemporalDuration(
      isolate, 0, 0, 0, 0, result_hours, result_minutes, result_seconds,
      result_milliseconds, result_microseconds, result_nanoseconds);
}

}  // namespace

// #sec-temporal.instant.prototype.until
MaybeHandle<JSTemporalDuration> JSTemporalInstant::Until(
    Isolate* isolate, Handle<JSTemporalInstant> handle, Handle<Object> other,
    Handle<Object> options) {
  TEMPORAL_ENTER_FUNC
  return UntilOrSince(isolate, handle, other, options, true,
                      "Temporal.Instant.prototype.until");
}

// #sec-temporal.instant.prototype.since
MaybeHandle<JSTemporalDuration> JSTemporalInstant::Since(
    Isolate* isolate, Handle<JSTemporalInstant> handle, Handle<Object> other,
    Handle<Object> options) {
  TEMPORAL_ENTER_FUNC
  return UntilOrSince(isolate, handle, other, options, false,
                      "Temporal.Instant.prototype.since");
}

// #sec-temporal.instant.prototype.round
MaybeHandle<JSTemporalInstant> JSTemporalInstant::Round(
    Isolate* isolate, Handle<JSTemporalInstant> handle,
    Handle<Object> options_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.Instant.prototype.round";
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. If options is undefined, then
  if (options_obj->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalInstant);
  }
  // 4. Set options to ? GetOptionsObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, options_obj, method),
                             JSTemporalInstant);
  // 5. Let smallestUnit be ? ToSmallestTemporalUnit(options, « "year", "month",
  // "week", "day" », undefined).
  Maybe<temporal::Unit> maybe_smallest_unit = ToSmallestTemporalUnit(
      isolate, options,
      std::set<temporal::Unit>({temporal::Unit::kYear, temporal::Unit::kMonth,
                                temporal::Unit::kWeek, temporal::Unit::kDay}),
      temporal::Unit::kNotPresent, method);
  MAYBE_RETURN(maybe_smallest_unit, Handle<JSTemporalInstant>());
  temporal::Unit smallest_unit = maybe_smallest_unit.FromJust();
  // 6. If smallestUnit is undefined, throw a RangeError exception.
  if (smallest_unit == temporal::Unit::kNotPresent) {
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_RANGE_ERROR(),
                    JSTemporalInstant);
  }
  // 7. Let roundingMode be ? ToTemporalRoundingMode(options, "halfExpand").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(
          isolate, options, temporal::RoundingMode::kHalfExpand, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<JSTemporalInstant>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();
  double maximum;
  switch (smallest_unit) {
    // 8. If smallestUnit is "hour", then
    case temporal::Unit::kHour:
      // a. Let maximum be 24.
      maximum = 24;
      break;
    // 9. Else if smallestUnit is "minute", then
    case temporal::Unit::kMinute:
      // a. Let maximum be 1440.
      maximum = 1440;
      break;
    // 10. Else if smallestUnit is "second", then
    case temporal::Unit::kSecond:
      // a. Let maximum be 86400.
      maximum = 86400;
      break;
    // 11. Else if smallestUnit is "millisecond", then
    case temporal::Unit::kMillisecond:
      // a. Let maximum be 8.64 × 107.
      maximum = 86400000;
      break;
    // 12. Else if smallestUnit is "microsecond", then
    case temporal::Unit::kMicrosecond:
      // a. Let maximum be 8.64 × 1010.
      maximum = 86400000000;
      break;
    // 13. Else,
    default:
      // a. Assert: smallestUnit is "nanosecond".
      CHECK_EQ(smallest_unit, temporal::Unit::kNanosecond);
      // b. Let maximum be 8.64 × 1013.
      maximum = 86400000000000;
      break;
  }
  // 14. Let roundingIncrement be ? ToTemporalRoundingIncrement(options,
  // maximum, true).
  Maybe<int> maybe_rounding_increment = temporal::ToTemporalRoundingIncrement(
      isolate, options, maximum, true, true, method);
  MAYBE_RETURN(maybe_rounding_increment, Handle<JSTemporalInstant>());
  int rounding_increment = maybe_rounding_increment.FromJust();
  // 15. Let roundedNs be ? RoundTemporalInstant(instant.[[Nanoseconds]],
  // roundingIncrement, smallestUnit, roundingMode).
  Handle<BigInt> rounded_ns;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, rounded_ns,
      RoundTemporalInstant(isolate,
                           Handle<BigInt>(handle->nanoseconds(), isolate),
                           rounding_increment, smallest_unit, rounding_mode),
      JSTemporalInstant);
  // 16. Return ! CreateTemporalInstant(roundedNs).
  return temporal::CreateTemporalInstant(isolate, rounded_ns);
}

// #sec-temporal.instant.prototype.equals
MaybeHandle<Oddball> JSTemporalInstant::Equals(Isolate* isolate,
                                               Handle<JSTemporalInstant> handle,
                                               Handle<Object> other_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.Instant.prototype.equals";
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. Set other to ? ToTemporalInstant(other).
  Handle<JSTemporalInstant> other;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, other, temporal::ToTemporalInstant(isolate, other_obj, method),
      Oddball);
  // 4. If instant.[[Nanoseconds]] ≠ other.[[Nanoseconds]], return false.
  // 5. Return true.
  return BigInt::CompareToBigInt(
             Handle<BigInt>(handle->nanoseconds(), isolate),
             Handle<BigInt>(other->nanoseconds(), isolate)) ==
                 ComparisonResult::kEqual
             ? isolate->factory()->true_value()
             : isolate->factory()->false_value();
}

MaybeHandle<String> JSTemporalInstant::ToString(
    Isolate* isolate, Handle<JSTemporalInstant> instant,
    Handle<Object> options_obj) {
  TEMPORAL_ENTER_FUNC
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  Handle<JSReceiver> options;
  const char* method = "Temporal.Instant.prototype.toString";
  // 3. Set options to ? GetOptionsObject(options).
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, GetOptionsObject(isolate, options_obj, method), String);
  // 4. Let timeZone be ? Get(options, "timeZone").
  Handle<Object> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      Object::GetPropertyOrElement(isolate, options,
                                   isolate->factory()->timeZone_string()),
      String);

  // 5. If timeZone is not undefined, then
  if (!time_zone->IsUndefined(isolate)) {
    // a. Set timeZone to ? ToTemporalTimeZone(timeZone).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone,
        temporal::ToTemporalTimeZone(isolate, time_zone, method), String);
  }

  // 6. Let precision be ? ToSecondsStringPrecision(options).
  temporal::Precision precision;
  double increment;
  temporal::Unit unit;
  Maybe<bool> maybe_precision = ToSecondsStringPrecision(
      isolate, options, &precision, &increment, &unit, method);
  MAYBE_RETURN(maybe_precision, Handle<String>());

  // 7. Let roundingMode be ? ToTemporalRoundingMode(options, "trunc").
  Maybe<temporal::RoundingMode> maybe_rounding_mode =
      temporal::ToTemporalRoundingMode(isolate, options,
                                       temporal::RoundingMode::kTrunc, method);
  MAYBE_RETURN(maybe_rounding_mode, Handle<String>());
  temporal::RoundingMode rounding_mode = maybe_rounding_mode.FromJust();

  // 8. Let ns be ℝ(instant.[[Nanoseconds]]).
  Handle<BigInt> ns = Handle<BigInt>(instant->nanoseconds(), isolate);

  // 9. Let roundedNs be ? RoundTemporalInstant(ns, precision.[[Increment]],
  // precision.[[Unit]], roundingMode).
  Handle<BigInt> rounded_ns;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, rounded_ns,
                             temporal::RoundTemporalInstant(
                                 isolate, ns, increment, unit, rounding_mode),
                             String);

  // 10. Let roundedInstant be ? CreateTemporalInstant(roundedNs).
  Handle<JSTemporalInstant> rounded_instant;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, rounded_instant,
      temporal::CreateTemporalInstant(isolate, rounded_ns), String);

  // 11. Return ? TemporalInstantToString(roundedInstant, timeZone,
  // precision.[[Precision]]).
  return TemporalInstantToString(isolate, rounded_instant, time_zone,
                                 precision);
}

// #sec-temporal.instant.prototype.tolocalestring
MaybeHandle<String> JSTemporalInstant::ToLocaleString(
    Isolate* isolate, Handle<JSTemporalInstant> handle, Handle<Object> locales,
    Handle<Object> options) {
  TEMPORAL_ENTER_FUNC
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. Return ? TemporalInstantToString(instant, undefined, "auto").
  return TemporalInstantToString(isolate, handle,
                                 isolate->factory()->undefined_value(),
                                 temporal::Precision::kAuto);
}

// #sec-temporal.instant.prototype.tojson
MaybeHandle<String> JSTemporalInstant::ToJSON(
    Isolate* isolate, Handle<JSTemporalInstant> handle) {
  TEMPORAL_ENTER_FUNC
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. Return ? TemporalInstantToString(instant, undefined, "auto").
  return TemporalInstantToString(isolate, handle,
                                 isolate->factory()->undefined_value(),
                                 temporal::Precision::kAuto);
}

// #sec-temporal.instant.prototype.tozoneddatetime
MaybeHandle<JSTemporalZonedDateTime> JSTemporalInstant::ToZonedDateTime(
    Isolate* isolate, Handle<JSTemporalInstant> handle,
    Handle<Object> item_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.Instant.prototype.toZonedDateTime";
  Factory* factory = isolate->factory();
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. If Type(item) is not Object, then
  if (!item_obj->IsJSReceiver()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalZonedDateTime);
  }
  Handle<JSReceiver> item = Handle<JSReceiver>::cast(item_obj);
  // 4. Let calendarLike be ? Get(item, "calendar").
  Handle<Object> calendar_like;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar_like,
      Object::GetPropertyOrElement(isolate, item, factory->calendar_string()),
      JSTemporalZonedDateTime);
  // 5. If calendarLike is undefined, then
  if (calendar_like->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalZonedDateTime);
  }
  // 6. Let calendar be ? ToTemporalCalendar(calendarLike).
  Handle<JSReceiver> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, calendar,
      temporal::ToTemporalCalendar(isolate, calendar_like, method),
      JSTemporalZonedDateTime);

  // 7. Let temporalTimeZoneLike be ? Get(item, "timeZone").
  Handle<Object> temporal_time_zone_like;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, temporal_time_zone_like,
      Object::GetPropertyOrElement(isolate, item, factory->timeZone_string()),
      JSTemporalZonedDateTime);
  // 8. If temporalTimeZoneLike is undefined, then
  if (calendar_like->IsUndefined()) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate, NEW_TEMPORAL_INVALD_ARG_TYPE_ERROR(),
                    JSTemporalZonedDateTime);
  }
  // 9. Let timeZone be ? ToTemporalTimeZone(temporalTimeZoneLike).
  Handle<JSReceiver> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      temporal::ToTemporalTimeZone(isolate, temporal_time_zone_like, method),
      JSTemporalZonedDateTime);
  // 10. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // calendar).
  return temporal::CreateTemporalZonedDateTime(
      isolate, Handle<BigInt>(handle->nanoseconds(), isolate), time_zone,
      calendar);
}

// #sec-temporal.instant.prototype.tozoneddatetimeiso
MaybeHandle<JSTemporalZonedDateTime> JSTemporalInstant::ToZonedDateTimeISO(
    Isolate* isolate, Handle<JSTemporalInstant> handle,
    Handle<Object> item_obj) {
  TEMPORAL_ENTER_FUNC
  const char* method = "Temporal.Instant.prototype.toZonedDateTimeISO";
  Factory* factory = isolate->factory();
  // 1. Let instant be the this value.
  // 2. Perform ? RequireInternalSlot(instant, [[InitializedTemporalInstant]]).
  // 3. If Type(item) is Object, then
  if (item_obj->IsJSReceiver()) {
    Handle<JSReceiver> item = Handle<JSReceiver>::cast(item_obj);
    // a. Let timeZoneProperty be ? Get(item, "timeZone").
    Handle<Object> time_zone_property;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, time_zone_property,
        Object::GetPropertyOrElement(isolate, item, factory->timeZone_string()),
        JSTemporalZonedDateTime);
    // b. If timeZoneProperty is not undefined, then
    if (!time_zone_property->IsUndefined()) {
      // i. Set item to timeZoneProperty.
      item_obj = time_zone_property;
    }
  }
  // 4. Let timeZone be ? ToTemporalTimeZone(item).
  Handle<JSReceiver> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone,
      temporal::ToTemporalTimeZone(isolate, item_obj, method),
      JSTemporalZonedDateTime);
  // 5. Let calendar be ! GetISO8601Calendar().
  Handle<JSTemporalCalendar> calendar;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, calendar,
                             temporal::GetISO8601Calendar(isolate),
                             JSTemporalZonedDateTime);
  // 6. Return ? CreateTemporalZonedDateTime(instant.[[Nanoseconds]], timeZone,
  // calendar).
  return temporal::CreateTemporalZonedDateTime(
      isolate, Handle<BigInt>(handle->nanoseconds(), isolate), time_zone,
      calendar);
}

}  // namespace internal
}  // namespace v8
