// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

d8.file.execute('test/mjsunit/temporal/temporal-helpers.js');

let d1 = new Temporal.PlainTime(1,2,3,4,5,6);
let badTime = { toPlainDateTime: d1.toPlainDateTime }
assertThrows(() => badTime.toPlainDateTime("2021-03-04"), TypeError,
    "Method Temporal.PlainTime.prototype.toPlainDateTime called on incompatible receiver #<Object>");

let invalidArgument = "invalid_argument";
let invalidTime = "Invalid time value";
assertThrows(() => d1.toPlainDateTime(null), RangeError, invalidTime);
assertThrows(() => d1.toPlainDateTime(undefined), RangeError, invalidTime);
assertThrows(() => d1.toPlainDateTime("string is invalid"), RangeError, invalidTime);
assertThrows(() => d1.toPlainDateTime(true), RangeError, invalidTime);
assertThrows(() => d1.toPlainDateTime(false), RangeError, invalidTime);
assertThrows(() => d1.toPlainDateTime(NaN), RangeError, invalidTime);
assertThrows(() => d1.toPlainDateTime(Infinity), RangeError, invalidTime);
assertThrows(() => d1.toPlainDateTime(123), RangeError, invalidTime);
assertThrows(() => d1.toPlainDateTime(456n), RangeError, invalidTime);
assertThrows(() => d1.toPlainDateTime(Symbol()), TypeError,
    "Cannot convert a Symbol value to a string");
assertThrows(() => d1.toPlainDateTime({}), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDateTime({hour: 23}), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDateTime({minute: 23}), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDateTime({second: 23}), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDateTime({millisecond: 23}), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDateTime({microecond: 23}), TypeError, invalidArgument);
assertThrows(() => d1.toPlainDateTime({nanosecond: 23}), TypeError, invalidArgument);

assertPlainDateTime(d1.toPlainDateTime("2021-10-15"),
    2021, 10, 15, 1, 2, 3, 4, 5, 6);
assertPlainDateTime(d1.toPlainDateTime({year: 2021, month: 10, day: 15}),
    2021, 10, 15, 1, 2, 3, 4, 5, 6);
assertPlainDateTime(d1.toPlainDateTime({year: 2021, monthCode: "M03", day: 15}),
    2021, 3, 15, 1, 2, 3, 4, 5, 6);
