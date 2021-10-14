// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

d8.file.execute('test/mjsunit/temporal/temporal-helpers.js');

assertPlainYearMonth(
    new Temporal.PlainYearMonth(1911, 10, undefined, 10),
    1911, 10, 10);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(2020, 3, undefined, 12),
    2020, 3, 12);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(1, 12, undefined, 25),
    1, 12, 25);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(1970, 1, undefined, 1),
    1970, 1, 1);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(-10, 12, undefined, 1),
    -10, 12, 1);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(-25406, 1, undefined, 1),
    -25406, 1, 1);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(26890, 12, undefined, 31),
    26890, 12, 31);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(1911, 10),
    1911, 10, 1);
assertPlainYearMonth(
    new Temporal.PlainYearMonth(-3000, 10),
    -3000, 10, 1);
