// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

let t1 = new Temporal.PlainYearMonth(2021, 3);
let t2 = new Temporal.PlainYearMonth(2021, 3);
let t3 = t1;
let t4 = new Temporal.PlainYearMonth(2021, 4);
assertEquals(0, Temporal.PlainYearMonth.compare(t1, t1));
assertEquals(0, Temporal.PlainYearMonth.compare(t1, t2));
assertEquals(0, Temporal.PlainYearMonth.compare(t1, t3));
assertEquals(0, Temporal.PlainYearMonth.compare(t1, "2021-03"));
assertEquals(0, Temporal.PlainYearMonth.compare(t1, "2021-03-14"));
assertEquals(0, Temporal.PlainYearMonth.compare(t1, "2021-03-14T23:59:59"));
assertEquals(1, Temporal.PlainYearMonth.compare(t4, t1));
assertEquals(-1, Temporal.PlainYearMonth.compare(t1, t4));
assertEquals(1, Temporal.PlainYearMonth.compare("2021-07-21", t1));

// Test Throw
assertThrows(() => Temporal.PlainYearMonth.compare(
    t1, "invalid iso8601 string"), RangeError);
assertThrows(() => Temporal.PlainYearMonth.compare(
    "invalid iso8601 string", t1), RangeError);
