// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEMPORAL_TEMPORAL_PARSER_H_
#define V8_TEMPORAL_TEMPORAL_PARSER_H_

#include "src/execution/isolate.h"

namespace v8 {
namespace internal {

struct ParsedResult {
  int32_t date_year;
  int32_t date_month;
  int32_t date_day;
  int32_t time_hour;
  int32_t time_minute;
  int32_t time_second;
  int32_t time_nanosecond;
  int32_t tzuo_sign;        // TimeZoneUTCOffsetSign
  int32_t tzuo_hour;        // TimeZoneUTCOffsetHour
  int32_t tzuo_minute;      // TimeZoneUTCOffsetMinute
  int32_t tzuo_second;      // TimeZoneUTCOffsetSecond
  int32_t tzuo_nanosecond;  // TimeZoneUTCOffsetFractionalPart
  bool utc_designator;
  std::string tzi_name;  // TimeZoneIANAName
  std::string calendar_name;

  ParsedResult() { this->clear(); }

  bool date_year_is_undefined() const { return date_year == kMinInt31; }
  bool date_month_is_undefined() const { return date_month == kMinInt31; }
  bool date_day_is_undefined() const { return date_day == kMinInt31; }
  bool time_hour_is_undefined() const { return time_hour == kMinInt31; }
  bool time_minute_is_undefined() const { return time_minute == kMinInt31; }
  bool time_second_is_undefined() const { return time_second == kMinInt31; }
  bool time_nanosecond_is_undefined() const {
    return time_nanosecond == kMinInt31;
  }
  bool tzuo_hour_is_undefined() const { return tzuo_hour == kMinInt31; }
  bool tzuo_minute_is_undefined() const { return tzuo_minute == kMinInt31; }
  bool tzuo_second_is_undefined() const { return tzuo_second == kMinInt31; }
  bool tzuo_sign_is_undefined() const { return tzuo_sign == kMinInt31; }
  bool tzuo_nanosecond_is_undefined() const {
    return tzuo_nanosecond == kMinInt31;
  }
  void clear_date_year() { date_year = kMinInt31; }
  void clear_date_month() { date_month = kMinInt31; }
  void clear_date_day() { date_day = kMinInt31; }
  void clear_time_hour() { time_hour = kMinInt31; }
  void clear_time_minute() { time_minute = kMinInt31; }
  void clear_time_second() { time_second = kMinInt31; }
  void clear_time_nanosecond() { time_nanosecond = kMinInt31; }
  void clear_tzuo_hour() { tzuo_hour = kMinInt31; }
  void clear_tzuo_minute() { tzuo_minute = kMinInt31; }
  void clear_tzuo_second() { tzuo_second = kMinInt31; }
  void clear_tzuo_nanosecond() { tzuo_nanosecond = kMinInt31; }
  void clear_tzuo_sign() { tzuo_sign = kMinInt31; }

  void clear() {
    clear_date_year();
    clear_date_month();
    clear_date_day();
    clear_time_hour();
    clear_time_minute();
    clear_time_second();
    clear_time_nanosecond();
    clear_tzuo_sign();
    clear_tzuo_hour();
    clear_tzuo_minute();
    clear_tzuo_second();
    clear_tzuo_nanosecond();
    utc_designator = false;
    tzi_name.clear();
    calendar_name.clear();
  }
};

struct ParsedDuration {
  int64_t sign;
  int64_t years;
  int64_t months;
  int64_t weeks;
  int64_t days;
  int64_t whole_hours;
  int64_t hours_fraction;  // in unit of 1 / 1e9 hours
  int64_t whole_minutes;
  int64_t minutes_fraction;  // in unit of 1 / 1e9 minutes
  int64_t whole_seconds;
  int64_t seconds_fraction;  // in unit of 1 / 1e9 seconds

  ParsedDuration() { this->clear(); }

  void clear() {
    sign = 1;
    years = months = weeks = days = whole_hours = hours_fraction =
        whole_minutes = minutes_fraction = whole_seconds = seconds_fraction = 0;
  }
};

#define DEFINE_PARSE_METHOD(R, NAME)                 \
  V8_WARN_UNUSED_RESULT static Maybe<R> Parse##NAME( \
      Isolate* isolate, Handle<String> iso_string, bool* valid)
class TemporalParser {
 public:
  DEFINE_PARSE_METHOD(ParsedResult, TemporalDateString);
  DEFINE_PARSE_METHOD(ParsedResult, TemporalDateTimeString);
  DEFINE_PARSE_METHOD(ParsedResult, TemporalTimeString);
  DEFINE_PARSE_METHOD(ParsedResult, TemporalYearMonthString);
  DEFINE_PARSE_METHOD(ParsedResult, TemporalMonthDayString);
  DEFINE_PARSE_METHOD(ParsedResult, TemporalInstantString);
  DEFINE_PARSE_METHOD(ParsedResult, TemporalZonedDateTimeString);
  DEFINE_PARSE_METHOD(ParsedResult, TemporalTimeZoneString);
  DEFINE_PARSE_METHOD(ParsedResult, TemporalRelativeToString);
  DEFINE_PARSE_METHOD(ParsedResult, TemporalCalendarString);
  DEFINE_PARSE_METHOD(ParsedDuration, TemporalDurationString);
  DEFINE_PARSE_METHOD(ParsedResult, TimeZoneNumericUTCOffset);
};
#undef DEFINE_PARSE_METHOD

}  // namespace internal
}  // namespace v8

#endif  // V8_TEMPORAL_TEMPORAL_PARSER_H_
