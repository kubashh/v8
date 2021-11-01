// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

let d1 = Temporal.Now.plainTimeISO();
// 1. Set options to ? GetOptionsObject(options).
[true, false, "string is invalid", Symbol(),
    123, 456n, Infinity, NaN, null].forEach(function(invalidOptions) {

  assertThrows(() => Temporal.PlainTime.from(
      d1, invalidOptions), TypeError);
    });

// a. Perform ? ToTemporalOverflow(options).
assertThrows(() => Temporal.PlainTime.from(
  d1, {overflow: "invalid overflow"}), RangeError);

[undefined, {}, {overflow: "constrain"}, {overflow: "reject"}].forEach(
    function(validOptions) {
  let d = new Temporal.PlainTime(1, 2, 3, 4, 5, 6);
  let d2 = Temporal.PlainTime.from(d, validOptions);
  assertEquals(1, d2.hour);
  assertEquals(2, d2.minute);
  assertEquals(3, d2.second);
  assertEquals(4, d2.millisecond);
  assertEquals(5, d2.microsecond);
  assertEquals(6, d2.nanosecond);
  assertEquals("iso8601", d2.calendar.id);
});

[undefined, {}, {overflow: "constrain"}, {overflow: "reject"}].forEach(
    function(validOptions) {
  let d3 = Temporal.PlainTime.from(
      {hour:9, minute: 8, second:7, millisecond:6, microsecond: 5, nanosecond: 4},
      validOptions);
  assertEquals(9, d3.hour);
  assertEquals(8, d3.minute);
  assertEquals(7, d3.second);
  assertEquals(6, d3.millisecond);
  assertEquals(5, d3.microsecond);
  assertEquals(4, d3.nanosecond);
  assertEquals("iso8601", d3.calendar.id);
});

[undefined, {}, {overflow: "constrain"}].forEach(
    function(validOptions) {
  let d4 = Temporal.PlainTime.from(
      {hour:24, minute: 60, second:60,
        millisecond:1000, microsecond: 1000, nanosecond: 1000},
      validOptions);
  assertEquals(23, d4.hour);
  assertEquals(59, d4.minute);
  assertEquals(59, d4.second);
  assertEquals(999, d4.millisecond);
  assertEquals(999, d4.microsecond);
  assertEquals(999, d4.nanosecond);
  assertEquals("iso8601", d4.calendar.id);
});

assertThrows(() => Temporal.PlainTime.from(
    {hour:24, minute: 60, second:60,
     millisecond:1000, microsecond: 1000, nanosecond: 1000},
    {overflow: "reject"}), RangeError);
