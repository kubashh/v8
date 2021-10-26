// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

let d1 = new Temporal.PlainYearMonth(2021, 2);
let d2 = Temporal.PlainYearMonth.from("2021-02");
let d3 = Temporal.PlainYearMonth.from("2021-01");

assertEquals(d1.equals(d2), true);
assertEquals(d1.equals(d3), false);
assertEquals(d2.equals(d3), false);

let badDate = {equals: d1.equals};
assertThrows(() => badDate.equals(d1), TypeError);
