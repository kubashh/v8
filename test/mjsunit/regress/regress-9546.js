// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertEquals(Math.hypot(Infinity, NaN), Infinity);
assertEquals(Math.hypot(NaN, 0), NaN);
assertEquals(Math.hypot(Infinity, NaN), Math.hypot(NaN, Infinity));
assertEquals(Math.hypot(NaN, 0), Math.hypot(0, NaN));
