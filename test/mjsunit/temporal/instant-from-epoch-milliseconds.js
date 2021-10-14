// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

let bigint_nano = 567890123456789000000n;
let milli = 567890123456789;
let inst1 = new Temporal.Instant(bigint_nano);
let inst2 = Temporal.Instant.fromEpochMilliseconds(milli);
assertEquals(inst1, inst2);

let just_fit_neg = -8640000000000000;
let just_fit_pos = 8640000000000000;
let too_big = 8640000000000001;
let too_small = -8640000000000001;

assertThrows(() =>
    {let inst = Temporal.Instant.fromEpochMilliseconds(too_small)},
    RangeError);
assertThrows(() =>
    {let inst = Temporal.Instant.fromEpochMilliseconds(too_big)},
    RangeError);
assertEquals(just_fit_neg,
    (Temporal.Instant.fromEpochMilliseconds(
        just_fit_neg)).epochMilliseconds);
assertEquals(just_fit_pos,
    (Temporal.Instant.fromEpochMilliseconds(
        just_fit_pos)).epochMilliseconds);
