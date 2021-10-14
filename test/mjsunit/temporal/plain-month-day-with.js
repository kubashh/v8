// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

d8.file.execute('test/mjsunit/temporal/temporal-helpers.js');

let d1 = new Temporal.PlainMonthDay(7, 10);
let badDate = { with: d1.with }
assertThrows(() => badDate.with(), TypeError);

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

assertPlainMonthDay(d1.with({month: 3, monthCode: "M03"}), "M03", 10);
assertPlainMonthDay(d1.with({monthCode: "M05"}), "M05", 10);
assertPlainMonthDay(d1.with({day: 1}), "M07", 1);
assertPlainMonthDay(d1.with({month: 3, year: 1956}), "M03", 10);

// only have month with monthCode or year
assertThrows(() => d1.with({month: 3}), TypeError);
// month and monthCode mismatch
assertThrows(() => d1.with({month: 3, monthCode: "M12"}), RangeError);
