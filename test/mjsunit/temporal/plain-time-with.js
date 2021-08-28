// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

d8.file.execute('test/mjsunit/temporal/temporal-helpers.js');

let d1 = new Temporal.PlainTime(1, 2, 3, 4, 5, 6);
let badTime = { with: d1.with }
assertThrows(() => badTime.with(), TypeError);

assertThrows(() => d1.with(null), TypeError);
assertThrows(() => d1.with(undefined), TypeError);
assertThrows(() => d1.with("string is invalid"), TypeError);
assertThrows(() => d1.with(true), TypeError);
assertThrows(() => d1.with(false), TypeError);
assertThrows(() => d1.with(NaN), TypeError);
assertThrows(() => d1.with(Infinity), TypeError);
assertThrows(() => d1.with(123), TypeError);
assertThrows(() => d1.with(456n), TypeError);
assertThrows(() => d1.with(Symbol()), TypeError);
let date = Temporal.Now.plainDateISO();
assertThrows(() => d1.with(date), TypeError);
let dateTime = Temporal.Now.plainDateTimeISO();
assertThrows(() => d1.with(dateTime), TypeError);
let time = Temporal.Now.plainTimeISO();
assertThrows(() => d1.with(time), TypeError);
let ym = new Temporal.PlainYearMonth(2021, 7);
assertThrows(() => d1.with(ym), TypeError);
let md = new Temporal.PlainMonthDay(12, 25);
assertThrows(() => d1.with(md), TypeError);
assertThrows(() => d1.with({calendar: "iso8601"}), TypeError);
assertThrows(() => d1.with({timeZone: "UTC"}), TypeError);
// options is not undefined or object
assertThrows(() => d1.with({day: 3}, null), TypeError);
assertThrows(() => d1.with({day: 3}, "string is invalid"), TypeError);
assertThrows(() => d1.with({day: 3}, true), TypeError);
assertThrows(() => d1.with({day: 3}, false), TypeError);
assertThrows(() => d1.with({day: 3}, 123), TypeError);
assertThrows(() => d1.with({day: 3}, 456n), TypeError);
assertThrows(() => d1.with({day: 3}, Symbol()), TypeError);
assertThrows(() => d1.with({day: 3}, NaN), TypeError);
assertThrows(() => d1.with({day: 3}, Infinity), TypeError);

assertThrows(() => d1.with({}), TypeError);
assertThrows(() => d1.with({year: 1234}), TypeError);
assertThrows(() => d1.with({month: 5}), TypeError);
assertThrows(() => d1.with({day: 6}), TypeError);

assertPlainTime(d1.with({hour:9}), 9, 2, 3, 4, 5, 6);
assertPlainTime(d1.with({minute:9}), 1, 9, 3, 4, 5, 6);
assertPlainTime(d1.with({second:9}), 1, 2, 9, 4, 5, 6);
assertPlainTime(d1.with({millisecond:987}), 1, 2, 3, 987, 5, 6);
assertPlainTime(d1.with({microsecond:987}), 1, 2, 3, 4, 987, 6);
assertPlainTime(d1.with({nanosecond:987}), 1, 2, 3, 4, 5, 987);
assertPlainTime(d1.with({hour: 9, millisecond: 123}), 9, 2, 3, 123, 5, 6);
assertPlainTime(d1.with({minute: 9, microsecond: 123}), 1, 9, 3, 4, 123, 6);
assertPlainTime(d1.with({second: 9, nanosecond: 123}), 1, 2, 9, 4, 5, 123);
