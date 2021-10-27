// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

d8.file.execute('test/mjsunit/temporal/temporal-helpers.js');

// Simple add
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .add("PT9H8M7.080090010S"), 10, 10, 10, 84, 95, 16);

assertPlainTime((new Temporal.PlainTime(0,0,0,0,1,996))
    .add("PT0.0000071S"), 0, 0, 0, 0, 9, 96);

assertPlainTime((new Temporal.PlainTime(0,0,0,1,996))
    .add("PT0.0071S"), 0, 0, 0, 9, 96, 0);

assertPlainTime((new Temporal.PlainTime(0,0,1,996))
    .add("PT7.1S"), 0, 0, 9, 96, 0, 0, 0);

assertPlainTime((new Temporal.PlainTime(0,1,59))
    .add("PT5M7S"), 0, 7, 6, 0, 0, 0);

assertPlainTime((new Temporal.PlainTime(1,59))
    .add("PT5H7M"), 7, 6, 0, 0, 0, 0);

assertPlainTime((new Temporal.PlainTime(19))
    .add("PT8H"),  3, 0, 0, 0, 0, 0);

assertPlainTime((new Temporal.PlainTime(21,52,53,994,995,996))
    .add("PT5H13M11.404303202S"), 3, 6, 5, 399, 299, 198);

assertPlainTime((new Temporal.PlainTime(0,0,0,0,0,995))
    .add("PT0.000000006S"), 0, 0, 0, 0, 1, 1);
assertPlainTime((new Temporal.PlainTime(0,0,0,0,0,995))
    .add("PT0.00000006S"), 0, 0, 0, 0, 1, 55);
assertPlainTime((new Temporal.PlainTime(0,0,0,0,0,995))
    .add("PT0.0000006S"), 0, 0, 0, 0, 1, 595);

assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .add("-PT0.000000007S"), 1, 2, 3, 4, 4, 999);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .add("-PT0.000005007S"), 1, 2, 3, 3, 999, 999);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .add("-PT0.004005007S"), 1, 2, 2, 999, 999, 999);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .add("-PT0.005006007S"), 1, 2, 2, 998, 998, 999);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .add("-PT4.005006007S"), 1, 1, 58, 998, 998, 999);

assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .add("-PT4S"), 1, 1, 59, 4, 5, 6);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .add("-PT5M"), 0, 57, 3, 4, 5, 6);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .add("-PT1H5M"), 23, 57, 3, 4, 5, 6);
assertPlainTime((new Temporal.PlainTime(1,2,3,4,5,6))
    .add("-PT1H5M4S"), 23, 56, 59, 4, 5, 6);

let goodTime = new Temporal.PlainTime(1,2,3);
let badTime = {add: goodTime.add};
assertThrows(() => badTime.add("PT30M"), TypeError);

// Throw in ToLimitedTemporalDuration
assertThrows(() => (new Temporal.PlainTime(1,2,3)).add("bad duration"),
    RangeError);
