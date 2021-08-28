// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

assertEquals("00:00:00", (new Temporal.PlainTime()).toJSON());
assertEquals("01:02:03", (new Temporal.PlainTime(1, 2, 3)).toJSON());
assertEquals("00:02:03", (new Temporal.PlainTime(0, 2, 3)).toJSON());
assertEquals("00:00:03", (new Temporal.PlainTime(0, 0, 3)).toJSON());
assertEquals("00:00:00", (new Temporal.PlainTime(0, 0, 0)).toJSON());
assertEquals("01:00:00", (new Temporal.PlainTime(1, 0, 0)).toJSON());
assertEquals("23:59:59", (new Temporal.PlainTime(23, 59, 59)).toJSON());
assertEquals("00:59:59", (new Temporal.PlainTime(0, 59, 59)).toJSON());
assertEquals("01:00:00.000000001",
    (new Temporal.PlainTime(1, 0, 0, 0, 0, 1)).toJSON());
assertEquals("01:00:00.000008009",
    (new Temporal.PlainTime(1, 0, 0, 0, 8, 9)).toJSON());
assertEquals("01:00:00.007008009",
    (new Temporal.PlainTime(1, 0, 0, 7, 8, 9)).toJSON());
assertEquals("01:00:00.00000009",
    (new Temporal.PlainTime(1, 0, 0, 0, 0, 90)).toJSON());
assertEquals("01:00:00.0000009",
    (new Temporal.PlainTime(1, 0, 0, 0, 0, 900)).toJSON());
assertEquals("01:00:00.000008",
    (new Temporal.PlainTime(1, 0, 0, 0, 8)).toJSON());
assertEquals("01:00:00.00008",
    (new Temporal.PlainTime(1, 0, 0, 0, 80)).toJSON());
assertEquals("01:00:00.0008",
    (new Temporal.PlainTime(1, 0, 0, 0, 800)).toJSON());
assertEquals("01:00:00.007", (new Temporal.PlainTime(1, 0, 0, 7)).toJSON());
assertEquals("01:00:00.07", (new Temporal.PlainTime(1, 0, 0, 70)).toJSON());
assertEquals("01:00:00.7", (new Temporal.PlainTime(1, 0, 0, 700)).toJSON());
assertEquals("01:00:00.000876",
    (new Temporal.PlainTime(1, 0, 0, 0, 876)).toJSON());
assertEquals("01:00:00.876", (new Temporal.PlainTime(1, 0, 0, 876)).toJSON());
assertEquals("01:00:00.000000876",
    (new Temporal.PlainTime(1, 0, 0, 0, 0, 876)).toJSON());
