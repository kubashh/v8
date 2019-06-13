// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var a;
try {
  a = new Uint8Array(0x70000000);
  a.x = 1;
} catch (e) {
  a = [];
}
assertThrows(()=>Object.entries(a), RangeError);
