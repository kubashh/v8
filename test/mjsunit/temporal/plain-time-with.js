// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

d8.file.execute('test/mjsunit/temporal/temporal-helpers.js');

let d1 = new Temporal.PlainTime(1, 2, 3, 4, 5, 6);
let badTime = { with: d1.with }
assertThrows(() => badTime.with(), TypeError,
    "Method Temporal.PlainTime.prototype.with called on incompatible receiver #<Object>");

let invalidArgument = "invalid_argument";
assertThrows(() => d1.with(null), TypeError, invalidArgument);
assertThrows(() => d1.with(undefined), TypeError, invalidArgument);
assertThrows(() => d1.with("string is invalid"), TypeError, invalidArgument);
assertThrows(() => d1.with(true), TypeError, invalidArgument);
assertThrows(() => d1.with(false), TypeError, invalidArgument);
assertThrows(() => d1.with(NaN), TypeError, invalidArgument);
assertThrows(() => d1.with(Infinity), TypeError, invalidArgument);
assertThrows(() => d1.with(123), TypeError, invalidArgument);
assertThrows(() => d1.with(456n), TypeError, invalidArgument);
assertThrows(() => d1.with(Symbol()), TypeError, invalidArgument);
let date = Temporal.Now.plainDateISO();
assertThrows(() => d1.with(date), TypeError, invalidArgument);
let dateTime = Temporal.Now.plainDateTimeISO();
assertThrows(() => d1.with(dateTime), TypeError, invalidArgument);
let time = Temporal.Now.plainTimeISO();
assertThrows(() => d1.with(time), TypeError, invalidArgument);
let ym = new Temporal.PlainYearMonth(2021, 7);
assertThrows(() => d1.with(ym), TypeError, invalidArgument);
let md = new Temporal.PlainMonthDay(12, 25);
assertThrows(() => d1.with(md), TypeError, invalidArgument);
assertThrows(() => d1.with({calendar: "iso8601"}), TypeError, invalidArgument);
assertThrows(() => d1.with({timeZone: "UTC"}), TypeError, invalidArgument);
// options is not undefined or object
assertThrows(() => d1.with({day: 3}, null), TypeError, invalidArgument);
assertThrows(() => d1.with({day: 3}, "string is invalid"), TypeError, invalidArgument);
assertThrows(() => d1.with({day: 3}, true), TypeError, invalidArgument);
assertThrows(() => d1.with({day: 3}, false), TypeError, invalidArgument);
assertThrows(() => d1.with({day: 3}, 123), TypeError, invalidArgument);
assertThrows(() => d1.with({day: 3}, 456n), TypeError, invalidArgument);
assertThrows(() => d1.with({day: 3}, Symbol()), TypeError, invalidArgument);
assertThrows(() => d1.with({day: 3}, NaN), TypeError, invalidArgument);
assertThrows(() => d1.with({day: 3}, Infinity), TypeError, invalidArgument);

assertThrows(() => d1.with({}), TypeError, invalidArgument);
assertThrows(() => d1.with({year: 1234}), TypeError, invalidArgument);
assertThrows(() => d1.with({month: 5}), TypeError, invalidArgument);
assertThrows(() => d1.with({day: 6}), TypeError, invalidArgument);

assertPlainTime(d1.with({hour:9}), 9, 2, 3, 4, 5, 6);
assertPlainTime(d1.with({minute:9}), 1, 9, 3, 4, 5, 6);
assertPlainTime(d1.with({second:9}), 1, 2, 9, 4, 5, 6);
assertPlainTime(d1.with({millisecond:987}), 1, 2, 3, 987, 5, 6);
assertPlainTime(d1.with({microsecond:987}), 1, 2, 3, 4, 987, 6);
assertPlainTime(d1.with({nanosecond:987}), 1, 2, 3, 4, 5, 987);
assertPlainTime(d1.with({hour: 9, millisecond: 123}), 9, 2, 3, 123, 5, 6);
assertPlainTime(d1.with({minute: 9, microsecond: 123}), 1, 9, 3, 4, 123, 6);
assertPlainTime(d1.with({second: 9, nanosecond: 123}), 1, 2, 9, 4, 5, 123);
