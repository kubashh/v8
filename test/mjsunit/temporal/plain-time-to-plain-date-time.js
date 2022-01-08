// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

d8.file.execute('test/mjsunit/temporal/temporal-helpers.js');

let d1 = new Temporal.PlainTime(1,2,3,4,5,6);
let badTime = { toPlainDateTime: d1.toPlainDateTime }
assertThrows(() => badTime.toPlainDateTime("2021-03-04"), TypeError);

assertThrows(() => d1.toPlainDateTime(null), RangeError);
assertThrows(() => d1.toPlainDateTime(undefined), RangeError);
assertThrows(() => d1.toPlainDateTime("string is invalid"), RangeError);
assertThrows(() => d1.toPlainDateTime(true), RangeError);
assertThrows(() => d1.toPlainDateTime(false), RangeError);
assertThrows(() => d1.toPlainDateTime(NaN), RangeError);
assertThrows(() => d1.toPlainDateTime(Infinity), RangeError);
assertThrows(() => d1.toPlainDateTime(123), RangeError);
assertThrows(() => d1.toPlainDateTime(456n), RangeError);
assertThrows(() => d1.toPlainDateTime(Symbol()), TypeError);
assertThrows(() => d1.toPlainDateTime({}), TypeError);
assertThrows(() => d1.toPlainDateTime({hour: 23}), TypeError);
assertThrows(() => d1.toPlainDateTime({minute: 23}), TypeError);
assertThrows(() => d1.toPlainDateTime({second: 23}), TypeError);
assertThrows(() => d1.toPlainDateTime({millisecond: 23}), TypeError);
assertThrows(() => d1.toPlainDateTime({microecond: 23}), TypeError);
assertThrows(() => d1.toPlainDateTime({nanosecond: 23}), TypeError);

assertPlainDateTime(d1.toPlainDateTime("2021-10-15"),
    2021, 10, 15, 1, 2, 3, 4, 5, 6);
assertPlainDateTime(d1.toPlainDateTime({year: 2021, month: 10, day: 15}),
    2021, 10, 15, 1, 2, 3, 4, 5, 6);
assertPlainDateTime(d1.toPlainDateTime({year: 2021, monthCode: "M03", day: 15}),
    2021, 3, 15, 1, 2, 3, 4, 5, 6);
