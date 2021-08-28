// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

let d1 = new Temporal.PlainTime(1,2,3,4,5,6);
let d2 = Temporal.PlainTime.from(
    {hour:1, minute:2, second:3, millisecond: 4, microsecond: 5, nanosecond: 6});
let d3 = Temporal.PlainTime.from(
    {hour:1, minute:2, second:3, millisecond: 4, microsecond: 5, nanosecond: 7});

assertEquals(d1.equals(d2), true);
assertEquals(d1.equals(d3), false);
assertEquals(d2.equals(d3), false);

let badDate = {equals: d1.equals};
assertThrows(() => badDate.equals(d1), TypeError);
