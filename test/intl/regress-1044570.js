// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test no crash with array of very long string.
let num = 0xAFFFFFF;
let expectedLength = 16 * (num + 2) + 2;
let lfm = new Intl.ListFormat();
var s = "a".repeat(num);
assertEquals(expectedLength, lfm.format(Array(16).fill(s)).length);
