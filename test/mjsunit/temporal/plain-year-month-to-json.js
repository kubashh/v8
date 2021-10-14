// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

// years in 4 digits range
assertEquals("2021-07", (new Temporal.PlainYearMonth(2021, 7)).toJSON());
assertEquals("9999-12", (new Temporal.PlainYearMonth(9999, 12)).toJSON());
assertEquals("1000-01", (new Temporal.PlainYearMonth(1000, 1)).toJSON());
assertEquals("2021-07",
    (new Temporal.PlainYearMonth(2021, 7, undefined, 3)).toJSON());
assertEquals("9999-12",
    (new Temporal.PlainYearMonth(9999, 12, undefined, 5)).toJSON());
assertEquals("1000-01",
    (new Temporal.PlainYearMonth(1000, 1, undefined, 23)).toJSON());

// years out of 4 digits range
assertEquals("+010000-01", (new Temporal.PlainYearMonth(10000, 1)).toJSON());
assertEquals("+025021-07", (new Temporal.PlainYearMonth(25021, 7)).toJSON());
assertEquals("+000999-12", (new Temporal.PlainYearMonth(999, 12)).toJSON());
assertEquals("+000099-08", (new Temporal.PlainYearMonth(99, 8)).toJSON());
assertEquals("-000020-09", (new Temporal.PlainYearMonth(-20, 9)).toJSON());
assertEquals("-002021-07", (new Temporal.PlainYearMonth(-2021, 7)).toJSON());
assertEquals("-022021-07", (new Temporal.PlainYearMonth(-22021, 7)).toJSON());

assertEquals("+010000-01",
    (new Temporal.PlainYearMonth(10000, 1, undefined, 3)).toJSON());
assertEquals("+025021-07",
    (new Temporal.PlainYearMonth(25021, 7, undefined, 1)).toJSON());
assertEquals("+000999-12",
    (new Temporal.PlainYearMonth(999, 12, undefined, 2)).toJSON());
assertEquals("+000099-08",
    (new Temporal.PlainYearMonth(99, 8, undefined, 31)).toJSON());
assertEquals("-000020-09",
    (new Temporal.PlainYearMonth(-20, 9, undefined, 10)).toJSON());
assertEquals("-002021-07",
    (new Temporal.PlainYearMonth(-2021, 7, undefined, 20)).toJSON());
assertEquals("-022021-07",
    (new Temporal.PlainYearMonth(-22021, 7, undefined, 23)).toJSON());

// Test non iso8601 calendar
