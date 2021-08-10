// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal


function assertPlainTime(time, hour, minute, second, millisecond, microsecond, nanosecond) {
  let fields = time.getISOFields();
  let keys = Object.keys(fields);
  assertEquals(7, keys.length);
  assertEquals("calendar", keys[0]);
  assertEquals("isoHour", keys[1]);
  assertEquals("isoMicrosecond", keys[2]);
  assertEquals("isoMillisecond", keys[3]);
  assertEquals("isoMinute", keys[4]);
  assertEquals("isoNanosecond", keys[5]);
  assertEquals("isoSecond", keys[6]);
  assertEquals(hour, fields.isoHour, "isoHour");
  assertEquals(minute, fields.isoMinute, "isoMinute");
  assertEquals(second, fields.isoSecond, "isoSecond");
  assertEquals(millisecond, fields.isoMillisecond, "isoMillisecond");
  assertEquals(microsecond, fields.isoMicrosecond, "isoMicrosecond");
  assertEquals(nanosecond, fields.isoNanosecond, "isoNanosecond");
  assertEquals(time.calendar, fields.calendar, "calendar");
}

// Simple subtract
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .subtract("-PT9H8M7.080090010S"), 10, 10, 10, 84, 95, 16);

assertPlainTime((new Temporal.PlainTime(0,0,0,0,1,996))
    .subtract("-PT0.0000071S"), 0, 0, 0, 0, 9, 96);

assertPlainTime((new Temporal.PlainTime(0,0,0,1,996))
    .subtract("-PT0.0071S"), 0, 0, 0, 9, 96, 0);

assertPlainTime((new Temporal.PlainTime(0,0,1,996))
    .subtract("-PT7.1S"), 0, 0, 9, 96, 0, 0, 0);

assertPlainTime((new Temporal.PlainTime(0,1,59))
    .subtract("-PT5M7S"), 0, 7, 6, 0, 0, 0);

assertPlainTime((new Temporal.PlainTime(1,59))
    .subtract("-PT5H7M"), 7, 6, 0, 0, 0, 0);

assertPlainTime((new Temporal.PlainTime(19))
    .subtract("-PT8H"),  3, 0, 0, 0, 0, 0);

assertPlainTime((new Temporal.PlainTime(21,52,53,994,995,996))
    .subtract("-PT5H13M11.404303202S"), 3, 6, 5, 399, 299, 198);

assertPlainTime((new Temporal.PlainTime(0,0,0,0,0,995))
    .subtract("-PT0.000000006S"), 0, 0, 0, 0, 1, 1);
assertPlainTime((new Temporal.PlainTime(0,0,0,0,0,995))
    .subtract("-PT0.00000006S"), 0, 0, 0, 0, 1, 55);
assertPlainTime((new Temporal.PlainTime(0,0,0,0,0,995))
    .subtract("-PT0.0000006S"), 0, 0, 0, 0, 1, 595);

assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .subtract("PT0.000000007S"), 1, 2, 3, 4, 4, 999);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .subtract("PT0.000005007S"), 1, 2, 3, 3, 999, 999);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .subtract("PT0.004005007S"), 1, 2, 2, 999, 999, 999);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .subtract("PT0.005006007S"), 1, 2, 2, 998, 998, 999);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .subtract("PT4.005006007S"), 1, 1, 58, 998, 998, 999);

assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .subtract("PT4S"), 1, 1, 59, 4, 5, 6);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .subtract("PT5M"), 0, 57, 3, 4, 5, 6);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .subtract("PT1H5M"), 23, 57, 3, 4, 5, 6);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .subtract("PT1H5M4S"), 23, 56, 59, 4, 5, 6);


let goodTime = new Temporal.PlainTime(1,2,3);
let badTime = {subtract: goodTime.subtract};
assertThrows(() => badTime.subtract("PT30M"), TypeError,
    "Method Temporal.PlainTime.prototype.subtract called on incompatible receiver #<Object>");

// Throw in ToLimitedTemporalDuration
assertThrows(() => (new Temporal.PlainTime(1,2,3)).subtract("bad duration"), RangeError,
    "Invalid time value");
