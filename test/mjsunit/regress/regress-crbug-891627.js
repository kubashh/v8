// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// We need a NumberModulus, so we make sure that we have a
// SpeculativeNumberModulus with Number feedback, and later
// on use it with known Number inputs (via the bitwise or),
// such that JSTypedLowering turns it into the NumberModulus.
function bar(x) { return x % 2; }
bar(0.1);

function foo(x) {
  // The NumberEqual identifies 0 and -0.
  return bar(x | -1) == 4294967295;
}

assertFalse(foo(1));
assertFalse(foo(0));
%OptimizeFunctionOnNextCall(foo);
assertFalse(foo(1));
assertFalse(foo(0));
