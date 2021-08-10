// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

function assertPlainTime(time, hour, minute, second, millisecond, microsecond, nanosecond) {
  assertEquals(hour, time.hour, time);
  assertEquals(minute, time.minute, time);
  assertEquals(second, time.second, time);
  assertEquals(millisecond, time.millisecond, time);
  assertEquals(microsecond, time.microsecond, time);
  assertEquals(nanosecond, time.nanosecond, time);
}
let d1 = new Temporal.PlainTime();
assertPlainTime(d1, 0, 0, 0, 0, 0, 0);

let d2 = new Temporal.PlainTime(1, 2, 3, 4, 5, 6);
assertPlainTime(d2, 1, 2, 3, 4, 5, 6);

let d3 = new Temporal.PlainTime(1, 2, 3, 4, 5);
assertPlainTime(d3, 1, 2, 3, 4, 5, 0);

let d4 = new Temporal.PlainTime(1, 2, 3, 4);
assertPlainTime(d4, 1, 2, 3, 4, 0, 0);

let d5 = new Temporal.PlainTime(1, 2, 3);
assertPlainTime(d5, 1, 2, 3, 0, 0, 0);

let d6 = new Temporal.PlainTime(1, 2);
assertPlainTime(d6, 1, 2, 0, 0, 0, 0);

let d7 = new Temporal.PlainTime(1, 0);
assertPlainTime(d7, 1, 0, 0, 0, 0, 0);

// smallest values
let d8 = new Temporal.PlainTime(0, 0, 0, 0, 0, 0);
assertPlainTime(d8, 0, 0, 0, 0, 0, 0);

// largest values
let d9 = new Temporal.PlainTime(23, 59, 59, 999, 999, 999);
assertPlainTime(d9, 23, 59, 59, 999, 999, 999);

let d10 = new Temporal.PlainTime(true, false, NaN, undefined, true);
assertPlainTime(d10, 1, 0, 0, 0, 1, 0);

let d11 = new Temporal.PlainTime(11.9, 12.8, 13.7, 14.6, 15.5, 1.999999);
assertPlainTime(d11, 11, 12, 13, 14, 15, 1);

assertThrows(() => new Temporal.PlainTime(-Infinity), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(Infinity), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(Symbol(2)), TypeError,
    "Cannot convert a Symbol value to a number");
assertThrows(() => new Temporal.PlainTime(3n), TypeError,
    "Cannot convert a BigInt value to a number");
assertThrows(() => new Temporal.PlainTime(24), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(0, 60), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(0, 0, 60), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(0, 0, 0, 1000), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(0, 0, 0, 0, 1000), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(0, 0, 0, 0, 0, 1000), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(-1), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(0, -1), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(0, 0, -1), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(0, 0, 0, -1), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(0, 0, 0, 0, -1), RangeError,
    "Invalid time value");
assertThrows(() => new Temporal.PlainTime(0, 0, 0, 0, 0, -1), RangeError,
    "Invalid time value");
