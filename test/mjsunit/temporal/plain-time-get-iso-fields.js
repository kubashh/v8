// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

d8.file.execute('test/mjsunit/temporal/temporal-helpers.js');

let d1 = new Temporal.PlainTime();
assertPlainTime(d1, 0, 0, 0, 0, 0, 0);

let d2 = new Temporal.PlainTime(1, 2, 3, 4, 5, 6);
assertPlainTime(d2, 1, 2, 3, 4, 5, 6);

let d3 = new Temporal.PlainTime(1, 2, 3, 4, 5);
assertPlainTime(d3, 1, 2, 3, 4, 5, 0);

let d4 = new Temporal.PlainTime(1, 2, 3, 4);
assertPlainTime(d4, 1, 2, 3, 4, 0, 0);

let d5 = new Temporal.PlainTime(1, 2, 3);
assertPlainTime(d5, 1, 2, 3, 0, 0, 0);

let d6 = new Temporal.PlainTime(1, 2);
assertPlainTime(d6, 1, 2, 0, 0, 0, 0);

let d7 = new Temporal.PlainTime(1, 0);
assertPlainTime(d7, 1, 0, 0, 0, 0, 0);

// smallest values
let dd8 = new Temporal.PlainTime(0, 0, 0, 0, 0, 0);
assertPlainTime(dd8, 0, 0, 0, 0, 0, 0);

// largest values
let d9 = new Temporal.PlainTime(23, 59, 59, 999, 999, 999);
assertPlainTime(d9, 23, 59, 59, 999, 999, 999);

let d10 = new Temporal.PlainTime(true, false, NaN, undefined, true);
assertPlainTime(d10, 1, 0, 0, 0, 1, 0);

let d11 = new Temporal.PlainTime(11.9, 12.8, 13.7, 14.6, 15.5, 1.999999);
assertPlainTime(d11, 11, 12, 13, 14, 15, 1);
