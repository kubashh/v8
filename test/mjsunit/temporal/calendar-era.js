// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

let cal = new Temporal.Calendar("iso8601");

let date = new Temporal.PlainDate(2021, 7, 15);
assertEquals(undefined, cal.era(date));
let dateTime = new Temporal.PlainDateTime(1997, 8, 23, 5, 30, 13);
assertEquals(undefined, cal.era(dateTime));
let monthDay = new Temporal.PlainMonthDay(2, 6);
assertThrows(() => cal.era(monthDay), TypeError);
assertEquals(undefined, cal.era("2019-03-18"));
