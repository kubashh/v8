// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-temporal

// Test Number.MAX_VALUE
// This is out of the range of Number.MAX_SAFE_INTEGER so the specification
// does not mandate the precision. But we should still check certain property of
// the result.
// Number.MAX_VALUE is 1.7976931348623157e+308 so the first 16 characters should
// be "P179769313486231" which is 15 digits and only require 50 bits so that
// should be precious in 64 bit floating point.
// There are total 309 digits so it should be 179769313486231 with another
// 294 digits (309-15 = 294)
assertTrue(/P179769313486231\d{294}Y/.test(
    (new Temporal.Duration(Number.MAX_VALUE)).toJSON()));
assertTrue(/-P179769313486231\d{294}Y/.test(
    (new Temporal.Duration(-Number.MAX_VALUE)).toJSON()));

assertTrue(/P179769313486231\d{294}M/.test(
    (new Temporal.Duration(0, Number.MAX_VALUE)).toJSON()));
assertTrue(/-P179769313486231\d{294}M/.test(
    (new Temporal.Duration(0, -Number.MAX_VALUE)).toJSON()));

assertTrue(/P179769313486231\d{294}W/.test(
    (new Temporal.Duration(0, 0, Number.MAX_VALUE)).toJSON()));
assertTrue(/-P179769313486231\d{294}W/.test(
    (new Temporal.Duration(0, 0, -Number.MAX_VALUE)).toJSON()));

assertTrue(/P179769313486231\d{294}D/.test(
    (new Temporal.Duration(0, 0, 0, Number.MAX_VALUE)).toJSON()));
assertTrue(/-P179769313486231\d{294}D/.test(
    (new Temporal.Duration(0, 0, 0, -Number.MAX_VALUE)).toJSON()));

assertTrue(/PT179769313486231\d{294}H/.test(
    (new Temporal.Duration(0, 0, 0, 0, Number.MAX_VALUE)).toJSON()));
assertTrue(/-PT179769313486231\d{294}H/.test(
    (new Temporal.Duration(0, 0, 0, 0, -Number.MAX_VALUE)).toJSON()));

assertTrue(/PT179769313486231\d{294}M/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, Number.MAX_VALUE)).toJSON()));
assertTrue(/-PT179769313486231\d{294}M/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, -Number.MAX_VALUE)).toJSON()));

assertTrue(/PT179769313486231\d{294}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, Number.MAX_VALUE)).toJSON()));
assertTrue(/-PT179769313486231\d{294}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, -Number.MAX_VALUE)).toJSON()));

// For millisecond, we should have 179769313486231 with another 291
// (309 - 15 - 3 = 291) digits, a '.', and then 3 digits
assertTrue(/PT179769313486231\d{291}[.]\d{3}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, Number.MAX_VALUE)).toJSON()));
assertTrue(/-PT179769313486231\d{291}[.]\d{3}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, -Number.MAX_VALUE)).toJSON()));

// For microsecond, we should have 179769313486231 with another 288
// (309 - 15 - 6 = 288) digits, a '.', and then 6 digits
assertTrue(/PT179769313486231\d{288}[.]\d{6}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, Number.MAX_VALUE))
    .toJSON()));
assertTrue(/-PT179769313486231\d{288}[.]\d{6}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, -Number.MAX_VALUE))
    .toJSON()));

// For nanosecond, we should have 179769313486231 with another 285
// (309 - 15 - 9 = 285) digits, a '.', and then 9 digits
assertTrue(/PT179769313486231\d{285}[.]\d{9}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, Number.MAX_VALUE))
    .toJSON()));
assertTrue(/-PT179769313486231\d{285}[.]\d{9}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, -Number.MAX_VALUE))
    .toJSON()));

// Test second + millisecond
// Number.MAX_VALUE + Number.MAX_VALUE / 1000 = 1.7994908279971777e+308
// So the first 17 characters should be "PT179949082799717" which is 15 digits
// and only require 50 bits so that should be precious in 64 bit floating point.
// For second and millisecond, we should have 179949082799717 with another 294
// digits, a '.', and then 3 digits
assertTrue(/PT179949082799717\d{294}[.]\d{3}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, Number.MAX_VALUE, Number.MAX_VALUE)).toJSON()));
assertTrue(/-PT179949082799717\d{294}[.]\d{3}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, -Number.MAX_VALUE, -Number.MAX_VALUE)).toJSON()));

// Test millisecond + microseconds
// For millisecond and microsecond, we should have 179949082799717 with another 291
// (309 - 15 - 3 = 291) digits, a '.', and then 6 digits
assertTrue(/PT179949082799717\d{291}[.]\d{6}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, Number.MAX_VALUE, Number.MAX_VALUE)).toJSON()));
assertTrue(/-PT179949082799717\d{291}[.]\d{6}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, -Number.MAX_VALUE, -Number.MAX_VALUE)).toJSON()));

// Test microseconds + nanoseconds
// For microsecond and nanosecond, we should have 179949082799717 with another 288
// (309 - 15 - 6 = 288) digits, a '.', and then 9 digits
assertTrue(/PT179949082799717\d{288}[.]\d{9}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, Number.MAX_VALUE, Number.MAX_VALUE)).toJSON()));
assertTrue(/-PT179949082799717\d{288}[.]\d{9}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, -Number.MAX_VALUE, -Number.MAX_VALUE)).toJSON()));

// Test second + millisecond + microseconds
// Number.MAX_VALUE + Number.MAX_VALUE / 1000 + Number.MAX_VALUE / 1000000 =
// 1.7994926256903124e+308
// So the first 17 characters should be "PT179949262569031" which is 15 digits
// and only require 50 bits so that should be precious in 64 bit floating point.
// For second and millisecond and microseconds, we should have 179949262569031 with another 294
// digits, a '.', and then 6 digits
assertTrue(/PT179949262569031\d{294}[.]\d{6}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, Number.MAX_VALUE, Number.MAX_VALUE, Number.MAX_VALUE)).toJSON()));
assertTrue(/-PT179949262569031\d{294}[.]\d{6}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, -Number.MAX_VALUE, -Number.MAX_VALUE, -Number.MAX_VALUE)).toJSON()));

// Test millisecond + microseconds + nanoseconds
// For millisecond and microsecond and nanoseconds, we should have 179949262569031 with another 291
// digits, a '.', and then 9 digits
assertTrue(/PT179949262569031\d{291}[.]\d{9}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, Number.MAX_VALUE, Number.MAX_VALUE, Number.MAX_VALUE)).toJSON()));
assertTrue(/-PT179949262569031\d{291}[.]\d{9}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, -Number.MAX_VALUE, -Number.MAX_VALUE, -Number.MAX_VALUE)).toJSON()));

// Test second + millisecond + microseconds + nanoseconds
// Number.MAX_VALUE + Number.MAX_VALUE / 1000 + Number.MAX_VALUE / 1000000 +
// Number.MAX_VALUE / 1000000000 = 1.7994926274880055e+308
// So the first 17 characters should be "PT179949262748800" which is 15 digits
// and only require 50 bits so that should be precious in 64 bit floating point.
// For second and millisecond and microseconds, and nanoseconds, we should have 179949262748800 with another 294
// digits, a '.', and then 9 digits
assertTrue(/PT179949262748800\d{294}[.]\d{9}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, Number.MAX_VALUE, Number.MAX_VALUE, Number.MAX_VALUE, Number.MAX_VALUE)).toJSON()));
assertTrue(/-PT179949262748800\d{294}[.]\d{9}S/.test(
    (new Temporal.Duration(0, 0, 0, 0, 0, 0, -Number.MAX_VALUE, -Number.MAX_VALUE, -Number.MAX_VALUE, -Number.MAX_VALUE)).toJSON()));
