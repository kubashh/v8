// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

// 1. Set options to ? GetOptionsObject(options).
[true, false, "string is invalid", Symbol(),
    123, 456n, Infinity, NaN, null].forEach(function(invalidOptions) {
      assertThrows(() => Temporal.PlainMonthDay.from(
          {monthCode: "M03", day: 1}, invalidOptions), TypeError);
    });

let d1 = new Temporal.PlainMonthDay(3, 16);
// a. Perform ? ToTemporalOverflow(options).
assertThrows(() => Temporal.PlainMonthDay.from(
  d1, {overflow: "invalid overflow"}), RangeError);

[undefined, {}, {overflow: "constrain"}, {overflow: "reject"}].forEach(
    function(validOptions) {
  let d2 = Temporal.PlainMonthDay.from(d1);
  assertEquals("M03", d2.monthCode);
  assertEquals(16, d2.day);
  assertEquals("iso8601", d2.calendar.id);
});

[undefined, {}, {overflow: "constrain"}, {overflow: "reject"}].forEach(
    function(validOptions) {
  let d3 = Temporal.PlainMonthDay.from({monthCode: "M04", day: 28}, validOptions);
  assertEquals("M04", d3.monthCode);
  assertEquals(28, d3.day);
  assertEquals("iso8601", d3.calendar.id);
});

[undefined, {}, {overflow: "constrain"}].forEach(function(validOptions) {
  let d4 = Temporal.PlainMonthDay.from({monthCode: "M02", day: 31}, validOptions);
  assertEquals("M02", d4.monthCode);
  assertEquals(29, d4.day);
  assertEquals("iso8601", d4.calendar.id);
});

assertThrows(() => Temporal.PlainMonthDay.from(
    {monthCode: "M02", day: 31}, {overflow: "reject"}),
    RangeError);
