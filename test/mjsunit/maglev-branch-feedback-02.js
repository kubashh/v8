// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --maglev-branch-feedback
// Flags: --allow-natives-syntax --maglev --no-always-sparkplug

function foo(a) {
  let b = 1;
  if (a != 0x333) { // Never taken.
    // Maglev can generate code for this branch even if it was never taken.
    b = 2;
  }
  return b;
}
%PrepareFunctionForOptimization(foo);

for (let i = 0; i < 100; ++i) {
  foo(0x333);
}

%OptimizeMaglevOnNextCall(foo);
assertEquals(1, foo(0x333));

assertTrue(isMaglevved(foo));

assertEquals(1, foo(0x333));
assertTrue(isMaglevved(foo));

assertEquals(2, foo(0));
assertFalse(isMaglevved(foo));
