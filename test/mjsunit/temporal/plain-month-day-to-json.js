// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

assertEquals("02-29", (new Temporal.PlainMonthDay(2, 29)).toJSON());
assertEquals("12-25", (new Temporal.PlainMonthDay(12, 25)).toJSON());
assertEquals("01-01", (new Temporal.PlainMonthDay(1, 1)).toJSON());
assertEquals("03-16", (new Temporal.PlainMonthDay(3, 16, undefined, 1967))
    .toJSON());
assertEquals("07-10", (new Temporal.PlainMonthDay(7, 10, undefined, 1964))
    .toJSON());
assertEquals("10-10", (new Temporal.PlainMonthDay(10, 10, undefined, 1911))
    .toJSON());

// Test non iso8601 calendar
