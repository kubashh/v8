// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

d8.file.execute('test/mjsunit/temporal/temporal-helpers.js');

assertPlainMonthDay(
    new Temporal.PlainMonthDay(10, 10, undefined, 1991), "M10",  10);
assertPlainMonthDay(
    new Temporal.PlainMonthDay(10, 10), "M10",  10);
assertPlainMonthDay(
    new Temporal.PlainMonthDay(3, 20, undefined, 2020), "M03", 20);
assertPlainMonthDay(
    new Temporal.PlainMonthDay(3, 20), "M03", 20);
assertPlainMonthDay(
    new Temporal.PlainMonthDay(12, 25, undefined, 1), "M12",  25);
assertPlainMonthDay(
    new Temporal.PlainMonthDay(12, 25), "M12",  25);
assertPlainMonthDay(
    new Temporal.PlainMonthDay(3, 15), "M03", 15);
assertPlainMonthDay(
    new Temporal.PlainMonthDay(2, 29), "M02",  29);
assertPlainMonthDay(
    new Temporal.PlainMonthDay(1, 1), "M01", 1);
assertPlainMonthDay(
    new Temporal.PlainMonthDay(12, 31), "M12", 31);

assertThrows(() => Temporal.PlainMonthDay(3, 16, undefined, 2021), TypeError);
assertThrows(() => Temporal.PlainMonthDay(3, 16), TypeError);
assertThrows(() => new Temporal.PlainMonthDay(), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(3), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(3, 0), RangeError);

assertPlainMonthDay(new Temporal.PlainMonthDay(3, 17), "M03", 17);

// Wrong month
assertThrows(() => new Temporal.PlainMonthDay(13, 7), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(13, 7, undefined, 2021),
    RangeError);
assertThrows(() => new Temporal.PlainMonthDay(0, 7), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(0, 7, undefined, 2021),
  RangeError);

// Wrong day for month
assertThrows(() => new Temporal.PlainMonthDay(1, 0), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(2, 0), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(3, 0), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(4, 0), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(5, 0), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(6, 0), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(7, 0), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(8, 0), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(9, 0), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(10, 0), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(11, 0), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(12, 0), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(1, 32), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(2, 30), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(3, 32), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(4, 31), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(5, 32), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(6, 31), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(7, 32), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(8, 32), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(9, 31), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(10, 32), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(11, 31), RangeError);
assertThrows(() => new Temporal.PlainMonthDay(12, 32), RangeError);
// Right day for month
assertPlainMonthDay((new Temporal.PlainMonthDay(1, 31)), "M01", 31);
assertPlainMonthDay((new Temporal.PlainMonthDay(2, 28)), "M02", 28);
assertPlainMonthDay((new Temporal.PlainMonthDay(3, 31)), "M03", 31);
assertPlainMonthDay((new Temporal.PlainMonthDay(4, 30)), "M04", 30);
assertPlainMonthDay((new Temporal.PlainMonthDay(5, 31)), "M05", 31);
assertPlainMonthDay((new Temporal.PlainMonthDay(6, 30)), "M06", 30);
assertPlainMonthDay((new Temporal.PlainMonthDay(7, 31)), "M07", 31);
assertPlainMonthDay((new Temporal.PlainMonthDay(8, 31)), "M08", 31);
assertPlainMonthDay((new Temporal.PlainMonthDay(9, 30)), "M09", 30);
assertPlainMonthDay((new Temporal.PlainMonthDay(10, 31)), "M10", 31);
assertPlainMonthDay((new Temporal.PlainMonthDay(11, 30)), "M11", 30);
assertPlainMonthDay((new Temporal.PlainMonthDay(12, 31)), "M12", 31);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(1, 31, undefined, 2021)), "M01", 31);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(2, 28, undefined, 2021)), "M02", 28);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(3, 31, undefined, 2021)), "M03", 31);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(4, 30, undefined, 2021)), "M04", 30);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(5, 31, undefined, 2021)), "M05", 31);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(6, 30, undefined, 2021)), "M06", 30);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(7, 31, undefined, 2021)), "M07", 31);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(8, 31, undefined, 2021)), "M08", 31);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(9, 30, undefined, 2021)), "M09", 30);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(10, 31, undefined, 2021)), "M10", 31);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(11, 30, undefined, 2021)), "M11", 30);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(12, 31, undefined, 2021)), "M12", 31);

// Check Feb 29 for Leap year
assertThrows(() =>
    new Temporal.PlainMonthDay(2, 29, undefined, 1990), RangeError);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(2, 29, undefined, 2000)), "M02", 29);

assertThrows(() =>
    new Temporal.PlainMonthDay(2, 29, undefined, 2001), RangeError);
assertThrows(() =>
    new Temporal.PlainMonthDay(2, 29, undefined, 2002), RangeError);
assertThrows(() =>
    new Temporal.PlainMonthDay(2, 29, undefined, 2003), RangeError);
assertPlainMonthDay(
    (new Temporal.PlainMonthDay(2, 29, undefined, 2004)), "M02", 29);
assertThrows(() =>
    new Temporal.PlainMonthDay(2, 29, undefined, 2100), RangeError);

// Infinty
assertThrows(() =>
    new Temporal.PlainMonthDay(Infinity, 28), RangeError);
assertThrows(() =>
    new Temporal.PlainMonthDay(-Infinity, 28), RangeError);
assertThrows(() =>
    new Temporal.PlainMonthDay(12, Infinity), RangeError);
assertThrows(() =>
    new Temporal.PlainMonthDay(12, -Infinity), RangeError);
assertThrows(() =>
    new Temporal.PlainMonthDay(Infinity, 28, undefined,  2021), RangeError);
assertThrows(() =>
    new Temporal.PlainMonthDay(-Infinity, 28, undefined, 2021), RangeError);
assertThrows(() =>
    new Temporal.PlainMonthDay(12, Infinity, undefined, 2021), RangeError);
assertThrows(() =>
    new Temporal.PlainMonthDay(12, -Infinity, undefined, 2021), RangeError);
assertThrows(() =>
    new Temporal.PlainMonthDay(3, 12, undefined, Infinity), RangeError);
assertThrows(() =>
    new Temporal.PlainMonthDay(3, 12, undefined, -Infinity), RangeError);

// TODO Test calendar
//
