// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/temporal/temporal-parser.h"

#include "src/strings/string-builder-inl.h"

namespace v8 {
namespace internal {

namespace {
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
#define CH_TO_INT(c) ((c) - '0')

// Hour:
//   [0 1] Digit
//   2 [0 1 2 3]
template <typename Char>
bool ScanHour(base::Vector<Char> str, int32_t s, std::string* out,
              int32_t* consumed) {
  if (str.length() < (s + 2)) return false;
  if (!((IN_RANGE('0', str[s], '1') && IS_DIGIT(str[s + 1])) ||
        ((str[s] == '2') && IN_RANGE('0', str[s + 1], '3'))))
    return false;
  (*out) = str[s];
  (*out) += str[s + 1];
  *consumed = 2;
  return true;
}
template <typename Char>
bool ScanHour(base::Vector<Char> str, int32_t s, int32_t* out,
              int32_t* consumed) {
  if (str.length() < (s + 2)) return false;
  if (!((IN_RANGE('0', str[s], '1') && IS_DIGIT(str[s + 1])) ||
        ((str[s] == '2') && IN_RANGE('0', str[s + 1], '3'))))
    return false;
  (*out) = CH_TO_INT(str[s]) * 10 + CH_TO_INT(str[s + 1]);
  *consumed = 2;
  return true;
}

// MinuteSecond:
//   [0 1 2 3 4 5] Digit
template <typename Char>
bool ScanMinuteSecond(base::Vector<Char> str, int32_t s, std::string* out,
                      int32_t* consumed) {
  if (str.length() < (s + 2)) return false;
  if (!(IN_RANGE('0', str[s], '5') && IS_DIGIT(str[s + 1]))) return false;
  (*out) = str[s];
  (*out) += str[s + 1];
  *consumed = 2;
  return true;
}

template <typename Char>
bool ScanMinuteSecond(base::Vector<Char> str, int32_t s, int32_t* out,
                      int32_t* consumed) {
  if (str.length() < (s + 2)) return false;
  if (!(IN_RANGE('0', str[s], '5') && IS_DIGIT(str[s + 1]))) return false;
  *out = CH_TO_INT(str[s]) * 10 + CH_TO_INT(str[s + 1]);
  *consumed = 2;
  return true;
}

#define SCAN_FORWARD(T1, T2, R)                                               \
  template <typename Char>                                                    \
  bool Scan##T1(base::Vector<Char> str, int32_t s, R* r, int32_t* consumed) { \
    bool ret = Scan##T2(str, s, r, consumed);                                 \
    return ret;                                                               \
  }

#define SCAN_EITHER_FORWARD(T1, T2, T3, R)                             \
  template <typename Char>                                             \
  bool Scan##T1(base::Vector<Char> str, int32_t s, R* r, int32_t* l) { \
    if (Scan##T2(str, s, r, l)) return true;                           \
    return Scan##T3(str, s, r, l);                                     \
  }

#define SCAN_FORWARD_TO_FIELD(T1, T2, field, R)                               \
  template <typename Char>                                                    \
  bool Scan##T1(base::Vector<Char> str, int32_t s, R* r, int32_t* consumed) { \
    return Scan##T2(str, s, &(r->field), consumed);                           \
  }

#define SCAN_EITHER_FORWARD_TO_FIELD(T1, T2, T3, field, R)                    \
  template <typename Char>                                                    \
  bool Scan##T1(base::Vector<Char> str, int32_t s, R* r, int32_t* consumed) { \
    return Scan##T2(str, s, &(r->field), consumed) ||                         \
           Scan##T3(str, s, &(r->field), consumed);                           \
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
                    int32_t* consumed) {
  if (ScanMinuteSecond(str, s, &(r->time_second), consumed)) {
    // MinuteSecond
    return true;
  }
  if (str.length() < (s + 2)) return false;
  if (('6' != str[s] || str[s + 1] != '0')) return false;
  // 60
  r->time_second = 60;
  *consumed = 2;
  return true;
}

// See PR1796
// FractionalPart : Digit{1,9}
template <typename Char>
bool ScanFractionalPart(base::Vector<Char> str, int32_t s, int64_t* out,
                        int32_t* consumed) {
  if (str.length() < (s + 1)) return false;
  if (!IS_DIGIT(str[s])) return false;
  *out = CH_TO_INT(str[s]);
  int32_t len = 1;
  while (((s + len) < str.length()) && (len < 9) && IS_DIGIT(str[s + len])) {
    *out = 10 * (*out) + CH_TO_INT(str[s + len]);
    len++;
  }
  for (int32_t i = len; i < 9; i++) {
    *out *= 10;
  }
  *consumed = len;
  return true;
}

template <typename Char>
bool ScanFractionalPart(base::Vector<Char> str, int32_t s, int32_t* out,
                        int32_t* consumed) {
  if (str.length() < (s + 1)) return false;
  if (!IS_DIGIT(str[s])) return false;
  *out = CH_TO_INT(str[s]);
  int32_t len = 1;
  while (((s + len) < str.length()) && (len < 9) && IS_DIGIT(str[s + len])) {
    *out = 10 * (*out) + CH_TO_INT(str[s + len]);
    len++;
  }
  for (int32_t i = len; i < 9; i++) {
    *out *= 10;
  }
  *consumed = len;
  return true;
}

// See PR1796
// TimeFraction: FractionalPart
SCAN_FORWARD_TO_FIELD(TimeFractionalPart, FractionalPart, time_nanosecond,
                      ParsedResult)

// Fraction: DecimalSeparator TimeFractionalPart
// DecimalSeparator: one of , .
template <typename Char>
bool ScanFraction(base::Vector<Char> str, int32_t s, ParsedResult* r,
                  int32_t* consumed) {
  if (str.length() < (s + 2)) return false;
  if (!(IS_DECIMAL_SEPARATOR(str[s]))) return false;
  if (!ScanTimeFractionalPart(str, s + 1, r, consumed)) return false;
  *consumed += 1;
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
                  int32_t* consumed) {
  int32_t hour_len = 0;
  if (!ScanTimeHour(str, s, r, &hour_len)) return false;
  if ((s + hour_len) == str.length()) {
    // TimeHour
    *consumed = hour_len;
    return true;
  }
  if (str[s + hour_len] == ':') {
    int32_t minute_len = 0;
    if (!ScanTimeMinute(str, s + hour_len + 1, r, &minute_len)) {
      r->clear_time_hour();
      return false;
    }
    if ((s + hour_len + 1 + minute_len) == str.length() ||
        (str[s + hour_len + 1 + minute_len] != ':')) {
      // TimeHour : TimeMinute
      *consumed = hour_len + 1 + minute_len;
      return true;
    }
    int32_t second_len = 0;
    if (!ScanTimeSecond(str, s + hour_len + 1 + minute_len + 1, r,
                        &second_len)) {
      r->clear_time_hour();
      r->clear_time_minute();
      return false;
    }
    int32_t fraction_len = 0;
    ScanTimeFraction(str, s + hour_len + 1 + minute_len + 1 + second_len, r,
                     &fraction_len);
    // TimeHour : TimeMinute : TimeSecond [TimeFraction]
    *consumed = hour_len + 1 + minute_len + 1 + second_len + fraction_len;
    return true;
  } else {
    int32_t minute_len;
    if (!ScanTimeMinute(str, s + hour_len, r, &minute_len)) {
      // TimeHour
      *consumed = hour_len;
      return true;
    }
    int32_t second_len = 0;
    if (!ScanTimeSecond(str, s + hour_len + minute_len, r, &second_len)) {
      // TimeHour TimeMinute
      *consumed = hour_len + minute_len;
      return true;
    }
    int32_t fraction_len = 0;
    ScanTimeFraction(str, s + hour_len + minute_len + second_len, r,
                     &fraction_len);
    // TimeHour TimeMinute TimeSecond [TimeFraction]
    *consumed = hour_len + minute_len + second_len + fraction_len;
    return true;
  }
}

// TimeSpecSeparator: DateTimeSeparator TimeSpec
// DateTimeSeparator: SPACE, 't', or 'T'
template <typename Char>
bool ScanTimeSpecSeparator(base::Vector<Char> str, int32_t s, ParsedResult* r,
                           int32_t* consumed) {
  if (!(((s + 1) < str.length()) && IS_DATE_TIME_SEPARATOR(str[s])))
    return false;
  int32_t len = 0;
  if (!ScanTimeSpec(str, s + 1, r, &len)) return false;
  *consumed = 1 + len;
  return true;
}

// DateExtendedYear: Sign Digit Digit Digit Digit Digit Digit
template <typename Char>
bool ScanDateExtendedYear(base::Vector<Char> str, int32_t s, int32_t* out_year,
                          int32_t* consumed) {
  if (str.length() < (s + 7)) {
    return false;
  }
  if (IS_SIGN(str[s]) && IS_DIGIT(str[s + 1]) && IS_DIGIT(str[s + 2]) &&
      IS_DIGIT(str[s + 3]) && IS_DIGIT(str[s + 4]) && IS_DIGIT(str[s + 5]) &&
      IS_DIGIT(str[s + 6])) {
    *consumed = 7;
    *out_year = IS_MINUS_SIGN(str[s]) ? -1 : 1;
    *out_year *= CH_TO_INT(str[s + 1]) * 100000 +
                 CH_TO_INT(str[s + 2]) * 10000 + CH_TO_INT(str[s + 3]) * 1000 +
                 CH_TO_INT(str[s + 4]) * 100 + CH_TO_INT(str[s + 5]) * 10 +
                 CH_TO_INT(str[s + 6]);
    return true;
  }
  return false;
}

// DateFourDigitYear: Digit Digit Digit Digit
template <typename Char>
bool ScanDateFourDigitYear(base::Vector<Char> str, int32_t s, int32_t* out_year,
                           int32_t* consumed) {
  if (str.length() < (s + 4)) return false;
  if (IS_DIGIT(str[s]) && IS_DIGIT(str[s + 1]) && IS_DIGIT(str[s + 2]) &&
      IS_DIGIT(str[s + 3])) {
    *consumed = 4;
    *out_year = CH_TO_INT(str[s]) * 1000 + CH_TO_INT(str[s + 1]) * 100 +
                CH_TO_INT(str[s + 2]) * 10 + CH_TO_INT(str[s + 3]);
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
                   int32_t* consumed) {
  if (str.length() < (s + 2)) return false;
  if (((str[s] == '0') && IS_NON_ZERO_DIGIT(str[s + 1])) ||
      ((str[s] == '1') && IN_RANGE('0', str[s + 1], '2'))) {
    *consumed = 2;
    r->date_month = CH_TO_INT(str[s]) * 10 + CH_TO_INT(str[s + 1]);
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
                 int32_t* consumed) {
  if (str.length() < (s + 2)) return false;
  if (((str[s] == '0') && IS_NON_ZERO_DIGIT(str[s + 1])) ||
      (IN_RANGE('1', str[s], '2') && IS_DIGIT(str[s + 1])) ||
      ((str[s] == '3') && IN_RANGE('0', str[s + 1], '1'))) {
    *consumed = 2;
    r->date_day = CH_TO_INT(str[s]) * 10 + CH_TO_INT(str[s + 1]);
    return true;
  }
  return false;
}

// Date:
//   DateYear - DateMonth - DateDay
//   DateYear DateMonth DateDay
template <typename Char>
bool ScanDate(base::Vector<Char> str, int32_t s, ParsedResult* r,
              int32_t* consumed) {
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
      r->clear_date_year();
      return false;
    }
    if (((s + year_len + 1 + month_len) == str.length()) ||
        (str[s + year_len + 1 + month_len] != '-')) {
      r->clear_date_year();
      r->clear_date_month();
      return false;
    }
    int32_t day_len;
    if (!ScanDateDay(str, s + year_len + 1 + month_len + 1, r, &day_len)) {
      r->clear_date_year();
      r->clear_date_month();
      return false;
    }
    // DateYear - DateMonth - DateDay
    *consumed = year_len + 1 + month_len + 1 + day_len;
    return true;
  } else {
    int32_t month_len;
    if (!ScanDateMonth(str, s + year_len, r, &month_len)) {
      r->clear_date_year();
      return false;
    }
    int32_t day_len;
    if (!ScanDateDay(str, s + year_len + month_len, r, &day_len)) {
      r->clear_date_year();
      r->clear_date_month();
      return false;
    }
    // DateYear DateMonth DateDay
    *consumed = year_len + month_len + day_len;
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
                      tzuo_nanosecond, ParsedResult)

// TimeZoneUTCOffsetFraction: DecimalSeparator TimeZoneUTCOffsetFractionalPart
// See PR1796
template <typename Char>
bool ScanTimeZoneUTCOffsetFraction(base::Vector<Char> str, int32_t s,
                                   ParsedResult* r, int32_t* consumed) {
  if (str.length() < (s + 2)) return false;
  if (!(IS_DECIMAL_SEPARATOR(str[s]))) return false;
  if (!ScanTimeZoneUTCOffsetFractionalPart(str, s + 1, r, consumed))
    return false;
  *consumed += 1;
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
                                  ParsedResult* r, int32_t* consumed) {
  if (str.length() < (s + 1)) return false;
  if (!IS_TIME_ZONE_UTC_OFFSET_SIGN(str[s])) return false;
  int32_t sign = (CANONICAL_SIGN(str[s]) == '-') ? -1 : 1;
  int32_t sign_len = 1;
  int32_t hour_len;
  if (!ScanTimeZoneUTCOffsetHour(str, s + sign_len, r, &hour_len)) return false;
  if ((s + sign_len + hour_len) == str.length()) {
    r->tzuo_sign = sign;
    *consumed = sign_len + hour_len;
    // TZUOSign TZUOHour
    return true;
  }
  if (str[s + sign_len + hour_len] == ':') {
    int32_t minute_len;
    if (!ScanTimeZoneUTCOffsetMinute(str, s + sign_len + hour_len + 1, r,
                                     &minute_len)) {
      r->clear_tzuo_hour();
      return false;
    }
    if ((s + sign_len + hour_len + 1 + minute_len) == str.length()) {
      // TZUOSign TZUOHour : TZUOMinute
      r->tzuo_sign = sign;
      *consumed = sign_len + hour_len + 1 + minute_len;
      return true;
    }
    if (str[s + sign_len + hour_len + 1 + minute_len] != ':') {
      r->tzuo_sign = sign;
      *consumed = sign_len + hour_len + 1 + minute_len;
      return true;
    }
    int32_t second_len;
    if (!ScanTimeZoneUTCOffsetSecond(
            str, s + sign_len + hour_len + 1 + minute_len + 1, r,
            &second_len)) {
      r->clear_tzuo_hour();
      r->clear_tzuo_minute();
      return false;
    }
    int32_t fraction_len = 0;
    ScanTimeZoneUTCOffsetFraction(
        str, s + sign_len + hour_len + 1 + minute_len + 1 + second_len, r,
        &fraction_len);
    // TZUOSign TZUOHour : TZUOMinute : TZUOSecond [TZUOFraction]
    r->tzuo_sign = sign;
    *consumed =
        sign_len + hour_len + 1 + minute_len + 1 + second_len + fraction_len;
    return true;
  } else {
    int32_t minute_len;
    if (!ScanTimeZoneUTCOffsetMinute(str, s + sign_len + hour_len, r,
                                     &minute_len)) {
      // TZUOSign TZUOHour
      r->tzuo_sign = sign;
      *consumed = sign_len + hour_len;
      return true;
    }
    int32_t second_len;
    if (!ScanTimeZoneUTCOffsetSecond(str, s + sign_len + hour_len + minute_len,
                                     r, &second_len)) {
      // TZUOSign TZUOHour TZUOMinute
      r->tzuo_sign = sign;
      *consumed = 1 + hour_len + minute_len;
      return true;
    }
    int32_t fraction_len = 0;
    ScanTimeZoneUTCOffsetFraction(
        str, s + sign_len + hour_len + minute_len + second_len, r,
        &fraction_len);
    // TZUOSign TZUOHour TZUOMinute TZUOSecond [TZUOFraction]
    r->tzuo_sign = sign;
    *consumed = sign_len + hour_len + minute_len + second_len + fraction_len;
    return true;
  }
}

// TimeZoneUTCOffset:
//   TimeZoneNumericUTCOffset
//   UTCDesignator
template <typename Char>
bool ScanTimeZoneUTCOffset(base::Vector<Char> str, int32_t s, ParsedResult* r,
                           int32_t* consumed) {
  if (str.length() < (s + 1)) return false;
  if (IS_UTC_DESIGNATOR(str[s])) {
    // UTCDesignator
    *consumed = 1;
    r->utc_designator = true;
    return true;
  }
  // TimeZoneNumericUTCOffset
  return ScanTimeZoneNumericUTCOffset(str, s, r, consumed);
}

// TimeZoneIANANameComponent
//   TZLeadingChar TZChar{0,13} but not one of . or ..
template <typename Char>
bool ScanTimeZoneIANANameComponent(base::Vector<Char> str, int32_t s,
                                   std::string* out, int32_t* consumed) {
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
  *consumed = len;
  return true;
}

// TimeZoneIANAName
//   TimeZoneIANANameComponent
//   TimeZoneIANANameComponent / TimeZoneIANAName
template <typename Char>
bool ScanTimeZoneIANAName(base::Vector<Char> str, int32_t s, std::string* out,
                          int32_t* consumed) {
  int32_t len1;
  if (!ScanTimeZoneIANANameComponent(str, s, out, &len1)) {
    out->clear();
    *consumed = 0;
    return false;
  }
  if ((str.length() < (s + len1 + 2)) || (str[s + len1] != '/')) {
    // TimeZoneIANANameComponent
    *consumed = len1;
    return true;
  }
  std::string part2;
  int32_t len2;
  if (!ScanTimeZoneIANANameComponent(str, s + len1 + 1, &part2, &len2)) {
    out->clear();
    *consumed = 0;
    return false;
  }
  // TimeZoneIANANameComponent / TimeZoneIANAName
  *out += '/' + part2;
  *consumed = len1 + 1 + len2;
  return true;
}

template <typename Char>
bool ScanTimeZoneIANAName(base::Vector<Char> str, int32_t s, ParsedResult* r,
                          int32_t* consumed) {
  return ScanTimeZoneIANAName(str, s, &(r->tzi_name), consumed);
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
                               std::string* out, int32_t* consumed) {
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
    *consumed = sign_len + hour_len;
    return true;
  }
  if (str[s + sign_len + hour_len] == ':') {
    // Sign Hour :
    int32_t minute_len = 0;
    std::string minute;
    if (!ScanMinuteSecond(str, s + sign_len + hour_len + 1, &minute,
                          &minute_len))
      return false;
    if ((s + sign_len + hour_len + 1 + minute_len) == str.length() ||
        (str[s + sign_len + hour_len + 1 + minute_len] != ':')) {
      // Sign Hour : MinuteSecond
      *out = sign + hour + ":" + minute;
      *consumed = sign_len + hour_len + 1 + minute_len;
      return true;
    }
    // Sign Hour : MinuteSecond :
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
    *consumed =
        sign_len + hour_len + 1 + minute_len + 1 + second_len + fraction_len;
    return true;
  } else {
    int32_t minute_len = 0;
    std::string minute;
    if (!ScanMinuteSecond(str, s + hour_len, &minute, &minute_len)) {
      // Sign Hour
      *out = sign + hour;
      *consumed = sign_len + hour_len;
      return true;
    }
    int32_t second_len = 0;
    std::string second;
    if (!ScanMinuteSecond(str, s + hour_len + minute_len, &second,
                          &second_len)) {
      // Sign Hour MinuteSecond
      *out = sign + hour + minute;
      *consumed = sign_len + hour_len + minute_len;
      return true;
    }
    int32_t fraction_len = 0;
    std::string fraction;
    // TODO(ftang) Problem See Issue 1794
    // ScanFraction(str, s + hour_len + minute_len + second_len, ??,
    // &fraction_len); Sign Hour MinuteSecond MinuteSecond [Fraction]
    *out = sign + hour + minute + second + fraction;
    *consumed = sign_len + hour_len + minute_len + second_len + fraction_len;
    return true;
  }
}

// TimeZoneBrackedName
//   TimeZoneIANAName
//   "Etc/GMT" ASCIISign Hour
//   TimeZoneUTCOffsetName
template <typename Char>
bool ScanTimeZoneBrackedName(base::Vector<Char> str, int32_t s, ParsedResult* r,
                             int32_t* consumed) {
  if (ScanTimeZoneIANAName(str, s, &(r->tzi_name), consumed)) {
    // TimeZoneIANAName
    return true;
  }
  if (ScanTimeZoneUTCOffsetName(str, s, &(r->tzi_name), consumed)) {
    // TimeZoneUTCOffsetName
    return true;
  }
  if ((s + 10) != str.length()) return false;
  if ((str[s] != 'E') || (str[s + 1] != 't') || (str[s + 2] != 'c') ||
      (str[s + 3] != '/') || (str[s + 4] != 'G') || (str[s + 5] != 'M') ||
      (str[s + 6] != 'T') || IS_ASCII_SIGN(str[s + 7]))
    return false;
  if (!ScanHour(str, s + 8, &(r->tzi_name), consumed)) return false;
  //   "Etc/GMT" ASCIISign Hour
  std::string etc_gmt = "Etc/GMT";
  etc_gmt += static_cast<char>(str[s + 7]);
  r->tzi_name = etc_gmt + r->tzi_name;
  *consumed += etc_gmt.length();
  return true;
}

// TimeZoneBrackedAnnotation: '[' TimeZoneBrackedName ']'
template <typename Char>
bool ScanTimeZoneBrackedAnnotation(base::Vector<Char> str, int32_t s,
                                   ParsedResult* r, int32_t* consumed) {
  if (str.length() < (s + 3)) return false;
  if (str[s] != '[') return false;
  if (!ScanTimeZoneBrackedName(str, s + 1, r, consumed)) return false;
  if (str[s + (*consumed) + 1] != ']') return false;
  *consumed += 2;
  return true;
}

// TimeZoneOffsetRequired:
//   TimeZoneUTCOffset [TimeZoneBrackedAnnotation]
template <typename Char>
bool ScanTimeZoneOffsetRequired(base::Vector<Char> str, int32_t s,
                                ParsedResult* r, int32_t* consumed) {
  int32_t len1;
  if (!ScanTimeZoneUTCOffset(str, s, r, &len1)) return false;
  int32_t len2 = 0;
  ScanTimeZoneBrackedAnnotation(str, s + len1, r, &len2);
  *consumed = len1 + len2;
  return true;
}

//   TimeZoneNameRequired:
//   [TimeZoneUTCOffset] TimeZoneBrackedAnnotation
template <typename Char>
bool ScanTimeZoneNameRequired(base::Vector<Char> str, int32_t s,
                              ParsedResult* r, int32_t* consumed) {
  int32_t len1 = 0;
  ScanTimeZoneUTCOffset(str, s, r, &len1);
  int32_t len2;
  if (!ScanTimeZoneBrackedAnnotation(str, s + len1, r, &len2)) return false;
  *consumed = len1 + len2;
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
              int32_t* consumed) {
  if (!ScanTimeSpec(str, s, r, consumed)) {
    return false;
  }
  int32_t time_zone_len = 0;
  ScanTimeZone(str, s, r, &time_zone_len);
  *consumed += time_zone_len;
  return true;
}

// DateTime: Date [TimeSpecSeparator][TimeZone]
template <typename Char>
bool ScanDateTime(base::Vector<Char> str, int32_t s, ParsedResult* r,
                  int32_t* consumed) {
  int32_t len1 = 0;
  if (!ScanDate(str, s, r, &len1)) return false;
  int32_t len2 = 0;
  ScanTimeSpecSeparator(str, s + len1, r, &len2);
  int32_t len3 = 0;
  ScanTimeZone(str, s + len1 + len2, r, &len3);
  *consumed = len1 + len2 + len3;
  return true;
}

// DateSpecYearMonth: DateYear ['-'] DateMonth
template <typename Char>
bool ScanDateSpecYearMonth(base::Vector<Char> str, int32_t s, ParsedResult* r,
                           int32_t* consumed) {
  int32_t year_len;
  if (!ScanDateYear(str, s, r, &year_len)) return false;
  int32_t sep_len = (str[s + year_len] == '-') ? 1 : 0;
  int32_t month_len;
  if (!ScanDateMonth(str, s + year_len + sep_len, r, &month_len)) {
    r->clear_date_year();
    return false;
  }
  *consumed = year_len + sep_len + month_len;
  return true;
}

// DateSpecMonthDay:
//  --opt DateMonth -opt DateDay
template <typename Char>
bool ScanDateSpecMonthDay(base::Vector<Char> str, int32_t s, ParsedResult* r,
                          int32_t* consumed) {
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
    r->clear_date_month();
    return false;
  }
  *consumed = prefix_len + month_len + delim_len + day_len;
  return true;
}

// CalendarNameComponent:
//   CalChar CalChar CalChar [CalChar CalChar CalChar CalChar CalChar]
template <typename Char>
bool ScanCalendarNameComponent(base::Vector<Char> str, int32_t s,
                               std::string* out, int32_t* consumed) {
  if (str.length() < (s + 3)) {
    *consumed = 0;
    return false;
  }
  if (!(IS_CAL_CHAR(str[s]) && IS_CAL_CHAR(str[s + 1]) &&
        IS_CAL_CHAR(str[s + 2]))) {
    *consumed = 0;
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
  *consumed = length;
  return true;
}

// CalendarName:
//   CalendarNameComponent
//   CalendarNameComponent - CalendarName
template <typename Char>
bool ScanCalendarName(base::Vector<Char> str, int32_t s, ParsedResult* r,
                      int32_t* consumed) {
  int32_t len1;
  if (!ScanCalendarNameComponent(str, s, &(r->calendar_name), &len1)) {
    r->calendar_name.clear();
    return false;
  }
  if ((str.length() < (s + len1 + 1)) || (str[s + len1] != '-')) {
    // CalendarNameComponent
    *consumed = len1;
    return true;
  }
  r->calendar_name += '-';
  int32_t len2;
  if (!ScanCalendarName(str, s + len1 + 1, r, &len2)) {
    r->calendar_name.clear();
    *consumed = 0;
    return false;
  }
  // CalendarNameComponent - CalendarName
  *consumed = len1 + 1 + len2;
  return true;
}

// Calendar: '[u-ca=' CalendarName ']'
template <typename Char>
bool ScanCalendar(base::Vector<Char> str, int32_t s, ParsedResult* r,
                  int32_t* consumed) {
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
  *consumed = 6 + calendar_name_len + 1;
  return true;
}

// TemporalTimeZoneIdentifier:
//   TimeZoneNumericUTCOffset
//   TimeZoneIANAName
template <typename Char>
bool ScanTemporalTimeZoneIdentifier(base::Vector<Char> str, int32_t s,
                                    ParsedResult* r, int32_t* consumed) {
  return ScanTimeZoneNumericUTCOffset(str, s, r, consumed) ||
         ScanTimeZoneIANAName(str, s, &(r->tzi_name), consumed);
}

// CalendarDateTime: DateTime [Calendar]
template <typename Char>
bool ScanCalendarDateTime(base::Vector<Char> str, int32_t s, ParsedResult* r,
                          int32_t* consumed) {
  int32_t date_time_len = 0;
  if (!ScanDateTime(str, 0, r, &date_time_len)) return false;
  int32_t calendar_len = 0;
  ScanCalendar(str, date_time_len, r, &calendar_len);
  *consumed = date_time_len + calendar_len;
  return true;
}

// TemporalZonedDateTimeString:
//   Date [TimeSpecSeparator] TimeZoneNameRequired [Calendar]
template <typename Char>
bool ScanTemporalZonedDateTimeString(base::Vector<Char> str, int32_t s,
                                     ParsedResult* r, int32_t* consumed) {
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
  *consumed =
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
                                                  int32_t* consumed) {
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
  *consumed = date_len + time_spec_len + time_zone_len + calendar_len;
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
                               ParsedResult* r, int32_t* consumed) {
  // Date
  int32_t date_len = 0;
  if (!ScanDate(str, s, r, &date_len)) return false;

  // TimeZoneOffsetRequired
  int32_t time_zone_offset_len;
  if (ScanTimeZoneOffsetRequired(str, s + date_len, r, &time_zone_offset_len)) {
    *consumed = date_len + time_zone_offset_len;
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
  *consumed =
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

SCAN_FORWARD(TimeFractionalPart, FractionalPart, int64_t)

template <typename Char>
bool ScanFraction(base::Vector<Char> str, int32_t s, int64_t* out,
                  int32_t* consumed) {
  if (str.length() < (s + 2)) return false;
  if (!(IS_DECIMAL_SEPARATOR(str[s]))) return false;
  if (!ScanTimeFractionalPart(str, s + 1, out, consumed)) return false;
  *consumed += 1;
  return true;
}

SCAN_FORWARD(TimeFraction, Fraction, int64_t)

// Digits :
//   Digit [Digits]

template <typename Char>
bool ScanDigits(base::Vector<Char> str, int32_t s, int64_t* out, int32_t* len) {
  int32_t l = 0;
  if (str.length() < (s + 1)) return false;
  if (!IS_DIGIT(str[s])) return false;
  *out = CH_TO_INT(str[s]);
  l++;
  while (s + l + 1 <= str.length() && IS_DIGIT(str[s + l])) {
    *out = 10 * (*out) + CH_TO_INT(str[s + l]);
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
      r->seconds_fraction = 0;
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
  int32_t part_len = 0;
  if (str.length() < (s + 1)) return false;
  if (!IS_TIME_DESIGNATOR(str[s])) return false;
  do {
    if (ScanDurationHoursPart(str, s + 1, r, &part_len)) break;
    r->whole_hours = r->hours_fraction = r->whole_minutes =
        r->minutes_fraction = r->whole_seconds = r->seconds_fraction = 0;

    if (ScanDurationMinutesPart(str, s + 1, r, &part_len)) break;
    r->whole_minutes = r->minutes_fraction = r->whole_seconds =
        r->seconds_fraction = 0;

    if (ScanDurationSecondsPart(str, s + 1, r, &part_len)) break;
    r->whole_seconds = r->seconds_fraction = 0;
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
  int32_t first_len = 0;
  if (!ScanDurationMonths(str, s, r, &first_len)) return false;

  if (str.length() < (s + first_len + 1)) return false;
  if (!IS_MONTHS_DESIGNATOR(str[s + first_len])) return false;
  int32_t second_len = 0;
  if (ScanDurationWeeksPart(str, s + first_len + 1, r, &second_len)) {
    *len = first_len + 1 + second_len;
    return true;
  }
  r->weeks = r->days = 0;
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
  r->months = r->weeks = r->days = 0;
  if (ScanDurationWeeksPart(str, s + 1 + first_len, r, &second_len)) {
    *len = first_len + 1 + second_len;
    return true;
  }
  // Reset failed attempt above.
  r->weeks = r->days = 0;
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
  int32_t first_len;

  do {
    if (ScanDurationYearsPart(str, s, r, &first_len)) break;
    r->years = r->months = r->weeks = r->days = 0;
    if (ScanDurationMonthsPart(str, s, r, &first_len)) break;
    r->months = r->weeks = r->days = 0;
    if (ScanDurationWeeksPart(str, s, r, &first_len)) break;
    r->weeks = r->days = 0;
    if (ScanDurationDaysPart(str, s, r, &first_len)) break;
    r->days = 0;
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
  int32_t first_len = 0;
  if (str.length() < (s + 2)) return false;
  int32_t sign = 1;
  if (IS_SIGN(str[s])) {
    sign = (CANONICAL_SIGN(str[s]) == '-') ? -1 : 1;
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
  r->years = r->months = r->weeks = r->days = 0;
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

#define IMPL_PARSE_METHOD(R, NAME)                                         \
  Maybe<R> TemporalParser::Parse##NAME(                                    \
      Isolate* isolate, Handle<String> iso_string, bool* valid) {          \
    R parsed;                                                              \
    iso_string = String::Flatten(isolate, iso_string);                     \
    {                                                                      \
      DisallowGarbageCollection no_gc;                                     \
      String::FlatContent str_content = iso_string->GetFlatContent(no_gc); \
      if (str_content.IsOneByte()) {                                       \
        *valid = Satisfy##NAME(str_content.ToOneByteVector(), &parsed);    \
      } else {                                                             \
        *valid = Satisfy##NAME(str_content.ToUC16Vector(), &parsed);       \
      }                                                                    \
    }                                                                      \
    return Just(parsed);                                                   \
  }

IMPL_PARSE_METHOD(ParsedResult, TemporalDateTimeString)
IMPL_PARSE_METHOD(ParsedResult, TemporalDateString)
IMPL_PARSE_METHOD(ParsedResult, TemporalYearMonthString)
IMPL_PARSE_METHOD(ParsedResult, TemporalMonthDayString)
IMPL_PARSE_METHOD(ParsedResult, TemporalTimeString)
IMPL_PARSE_METHOD(ParsedResult, TemporalInstantString)
IMPL_PARSE_METHOD(ParsedResult, TemporalZonedDateTimeString)
IMPL_PARSE_METHOD(ParsedResult, TemporalTimeZoneString)
IMPL_PARSE_METHOD(ParsedResult, TemporalRelativeToString)
IMPL_PARSE_METHOD(ParsedResult, TemporalCalendarString)
IMPL_PARSE_METHOD(ParsedResult, TimeZoneNumericUTCOffset)
IMPL_PARSE_METHOD(ParsedDuration, TemporalDurationString)

}  // namespace internal
}  // namespace v8
