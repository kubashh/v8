// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

let t1 = new Temporal.PlainTime(1, 3, 14);
let t2 = new Temporal.PlainTime(1, 3, 14);
let t3 = t1;
let t4 = new Temporal.PlainTime(1, 3, 15);
let t5 = new Temporal.PlainTime(1, 4, 14);
let t6 = new Temporal.PlainTime(2, 3, 14);
// years in 4 digits range
assertEquals(0, Temporal.PlainTime.compare(t1, t1));
assertEquals(0, Temporal.PlainTime.compare(t1, t2));
assertEquals(0, Temporal.PlainTime.compare(t1, t3));
assertEquals(0, Temporal.PlainTime.compare(t1, "01:03:14"));
assertEquals(0, Temporal.PlainTime.compare(t1, "2021-07-08T01:03:14"));
assertEquals(1, Temporal.PlainTime.compare(t4, t1));
assertEquals(1, Temporal.PlainTime.compare(t5, t1));
assertEquals(1, Temporal.PlainTime.compare(t6, t1));
assertEquals(-1, Temporal.PlainTime.compare(t1, t4));
assertEquals(-1, Temporal.PlainTime.compare(t1, t5));
assertEquals(-1, Temporal.PlainTime.compare(t1, t6));
assertEquals(1, Temporal.PlainTime.compare("02:07:08", t1));
assertEquals(-1, Temporal.PlainTime.compare(
    "2021-07-08T01:02:03", "2021-07-08T01:02:04"));
assertEquals(-1, Temporal.PlainTime.compare(
    "2021-07-08T01:02:03", "2021-07-07T01:02:04"));

// Test Throw
assertThrows(() => Temporal.PlainTime.compare(t1, "invalid iso8601 string"),
    RangeError);
assertThrows(() => Temporal.PlainTime.compare("invalid iso8601 string", t1),
    RangeError);
