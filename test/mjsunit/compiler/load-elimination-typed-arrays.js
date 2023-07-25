// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-load-elimination

function f(u32s, u8s) {
  u32s[0] = 0xffffffff;
  // The following store will overlap with the 1st store, and should thus
  // invalidate the Load Elimination state associated with the 1st store.
  u8s[1] = 0;
  // The following load should not be eliminated.
  return u32s[0];
}

const u32s = Uint32Array.of (3, 8);
const u8s = new Uint8Array(u32s.buffer);

%PrepareFunctionForOptimization(f);
assertEquals(f(u32s, u8s), 4294902015);
%OptimizeFunctionOnNextCall(f);
assertEquals(f(u32s, u8s), 4294902015);
