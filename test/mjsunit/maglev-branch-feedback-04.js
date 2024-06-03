// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --maglev-branch-feedback
// Flags: --allow-natives-syntax --maglev --sparkplug

function foo(a) {
  let b = 1;
  // "Then" block always executed (the jump over it never executed).
  if (a != 0x33) {
    b = 2;
  }
  return b;
}
%PrepareFunctionForOptimization(foo);
%CompileBaseline(foo);

for (let i = 0; i < 100; ++i) {
  foo(0);
}

%OptimizeMaglevOnNextCall(foo);
assertEquals(2, foo(0));

assertTrue(isMaglevved(foo));

assertEquals(2, foo(0));
assertTrue(isMaglevved(foo));

assertEquals(1, foo(0x33));
assertFalse(isMaglevved(foo));
