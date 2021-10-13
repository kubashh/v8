// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

d8.file.execute('test/mjsunit/temporal/temporal-helpers.js');

assertPlainYearMonth(
    new Temporal.PlainYearMonth(1911, 10, undefined, 10), 1911, 10,  10);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(2020, 3, undefined, 12), 2020, 3, 12);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(1, 12, undefined, 25), 1, 12,  25);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(1970, 1), 1970, 1, 1);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(-10, 12), -10, 12,  1);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(-25406, 1), -25406, 1, 1);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(26890, 12, undefined, 31), 26890, 12, 31);

assertThrows(() => Temporal.PlainYearMonth(2021, 7, undefined, 1), TypeError);
assertThrows(() => new Temporal.PlainYearMonth(), RangeError);
assertThrows(() => new Temporal.PlainYearMonth(2021), RangeError);
assertThrows(() => new Temporal.PlainYearMonth(2021, 0), RangeError);

assertPlainYearMonth(new Temporal.PlainYearMonth(2021, 7), 2021, 7, 1);

assertThrows(() => new Temporal.PlainYearMonth(2021, 13), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 7, undefined, 0), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 7, undefined, 32), RangeError);
assertThrows(() => new Temporal.PlainYearMonth(2021, -7), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, -7, undefined, 1), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, -7, undefined, -1), RangeError);
// Wrong month
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 0, undefined, 1), RangeError);
assertThrows(() => new Temporal.PlainYearMonth(2021, 0), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 0, undefined), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 13, undefined, 1), RangeError);
assertThrows(() => new Temporal.PlainYearMonth(2021, 13), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 13, undefined), RangeError);
// Right day for month
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2021, 1, undefined, 31)), 2021, 1, 31);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2021, 2, undefined, 28)), 2021, 2, 28);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2021, 3, undefined, 31)), 2021, 3, 31);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2021, 4, undefined, 30)), 2021, 4, 30);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2021, 5, undefined, 31)), 2021, 5, 31);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2021, 6, undefined, 30)), 2021, 6, 30);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2021, 7, undefined, 31)), 2021, 7, 31);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2021, 8, undefined, 31)), 2021, 8, 31);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2021, 9, undefined, 30)), 2021, 9, 30);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2021, 10, undefined, 31)), 2021, 10, 31);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2021, 11, undefined, 30)), 2021, 11, 30);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2021, 12, undefined, 31)), 2021, 12, 31);

assertPlainYearMonth((new Temporal.PlainYearMonth(2021, 1)), 2021, 1, 1);
assertPlainYearMonth((new Temporal.PlainYearMonth(2021, 2)), 2021, 2, 1);
assertPlainYearMonth((new Temporal.PlainYearMonth(2021, 3)), 2021, 3, 1);
assertPlainYearMonth((new Temporal.PlainYearMonth(2021, 4)), 2021, 4, 1);
assertPlainYearMonth((new Temporal.PlainYearMonth(2021, 5)), 2021, 5, 1);
assertPlainYearMonth((new Temporal.PlainYearMonth(2021, 6)), 2021, 6, 1);
assertPlainYearMonth((new Temporal.PlainYearMonth(2021, 7)), 2021, 7, 1);
assertPlainYearMonth((new Temporal.PlainYearMonth(2021, 8)), 2021, 8, 1);
assertPlainYearMonth((new Temporal.PlainYearMonth(2021, 9)), 2021, 9, 1);
assertPlainYearMonth((new Temporal.PlainYearMonth(2021, 10)), 2021, 10, 1);
assertPlainYearMonth((new Temporal.PlainYearMonth(2021, 11)), 2021, 11, 1);
assertPlainYearMonth((new Temporal.PlainYearMonth(2021, 12)), 2021, 12, 1);

// Check Feb 29 for Leap year
assertThrows(() =>
    new Temporal.PlainYearMonth(1900, 2, undefined, 29), RangeError);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2000, 2, undefined, 29)), 2000, 2, 29);

assertThrows(() =>
    new Temporal.PlainYearMonth(2001, 2, undefined, 29), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2002, 2, undefined, 29), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2003, 2, undefined, 29), RangeError);
assertPlainYearMonth(
    (new Temporal.PlainYearMonth(2004, 2, undefined, 29)), 2004, 2, 29);
assertThrows(() =>
    new Temporal.PlainYearMonth(2100, 2, undefined, 29), RangeError);

// Wrong day for month
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 1, undefined, 32), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 2, undefined, 29), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 3, undefined, 32), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 4, undefined, 31), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 5, undefined, 32), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 6, undefined, 31), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 7, undefined, 32), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 8, undefined, 32), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 9, undefined, 31), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 10, undefined, 32), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 11, undefined, 31), RangeError);
assertThrows(() =>
    new Temporal.PlainYearMonth(2021, 12, undefined, 32), RangeError);

// Infinty
assertThrows(() => new Temporal.PlainYearMonth(Infinity, 12), RangeError);
assertThrows(() => new Temporal.PlainYearMonth(-Infinity, 12), RangeError);
assertThrows(() => new Temporal.PlainYearMonth(Infinity, 12, undefined,  1),
    RangeError);
assertThrows(() => new Temporal.PlainYearMonth(-Infinity, 12, undefined, 1),
    RangeError);
assertThrows(() => new Temporal.PlainYearMonth(2021, 12, undefined, Infinity),
    RangeError);
assertThrows(() => new Temporal.PlainYearMonth(2021, 12, undefined, -Infinity),
    RangeError);
assertThrows(() => new Temporal.PlainYearMonth(2021, -Infinity, 1), RangeError);
assertThrows(() => new Temporal.PlainYearMonth(2021, Infinity, 1), RangeError);
assertThrows(() => new Temporal.PlainYearMonth(2021, -Infinity, undefined, 1),
    RangeError);
assertThrows(() => new Temporal.PlainYearMonth(2021, Infinity, undefined, 1),
    RangeError);

// TODO Test calendar
//
