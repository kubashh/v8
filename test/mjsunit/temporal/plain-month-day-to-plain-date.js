// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

d8.file.execute('test/mjsunit/temporal/temporal-helpers.js');

let d1 = new Temporal.PlainMonthDay(3, 10);
let badMonthDay = { toPlainDate: d1.toPlainDate }
assertThrows(() => badMonthDay.toPlainDate({day: 3}), TypeError);

assertThrows(() => d1.toPlainDate(null), TypeError);
assertThrows(() => d1.toPlainDate(undefined), TypeError);
assertThrows(() => d1.toPlainDate("string is invalid"), TypeError);
assertThrows(() => d1.toPlainDate(true), TypeError);
assertThrows(() => d1.toPlainDate(false), TypeError);
assertThrows(() => d1.toPlainDate(NaN), TypeError);
assertThrows(() => d1.toPlainDate(Infinity), TypeError);
assertThrows(() => d1.toPlainDate(123), TypeError);
assertThrows(() => d1.toPlainDate(456n), TypeError);
assertThrows(() => d1.toPlainDate(Symbol()), TypeError);
assertThrows(() => d1.toPlainDate({}), TypeError);
assertThrows(() => d1.toPlainDate({day: 23}), TypeError);
assertThrows(() => d1.toPlainDate({month: 12}), TypeError);
assertThrows(() => d1.toPlainDate({monthCode: "M12"}), TypeError);
assertThrows(() => d1.toPlainDate({hour: 23}), TypeError);

assertPlainDate(d1.toPlainDate({year: 1993}), 1993, 3, 10);
assertPlainDate(d1.toPlainDate({year: 1993, day: 15}), 1993, 3, 10);
assertPlainDate(d1.toPlainDate({year: 1993, month: 5}), 1993, 3, 10);
assertPlainDate(d1.toPlainDate({year: 1993, monthCode: "M07"}), 1993, 3, 10);
