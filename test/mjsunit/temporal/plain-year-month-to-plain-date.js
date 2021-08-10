// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

let d1 = new Temporal.PlainYearMonth(2021, 10);
let badYearMonth = { toPlainDate: d1.toPlainDate }
assertThrows(() => badYearMonth.toPlainDate({day: 3}), TypeError,
    "Method Temporal.PlainYearMonth.prototype.toPlainDate called on incompatible receiver #<Object>");

let invalidArgument = "invalid_argument";
assertThrows(() => d1.toPlainDate(null), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate(undefined), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate("string is invalid"), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate(true), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate(false), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate(NaN), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate(Infinity), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate(123), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate(456n), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate(Symbol()), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate({}), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate({year: 1999}), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate({month: 12}), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate({monthCode: "M12"}), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDate({hour: 23}), TypeError, invalidArgument);

function assertPlainDate(date, year, month, day) {
  assertEquals(year, date.year, "year");
  assertEquals(month, date.month, "month");
  assertEquals(day, date.day, "day");
  assertEquals(date.calendar, date.calendar, "calendar");
}

assertPlainDate(d1.toPlainDate({day: 15}), 2021, 10, 15);
assertPlainDate(d1.toPlainDate({year: 1993, day: 15}), 2021, 10, 15);
assertPlainDate(d1.toPlainDate({month: 4, day: 15}), 2021, 10, 15);
assertPlainDate(d1.toPlainDate({monthCode: "M04", day: 15}), 2021, 10, 15);
