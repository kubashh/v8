// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt

function f(x) {
  return BigInt.asUintN(3, x);
}

%PrepareFunctionForOptimization(f);
assertEquals(7n, f(7n));
assertEquals(1n, f(9n));
%OptimizeFunctionOnNextCall(f);
assertEquals(7n, f(7n));
assertEquals(1n, f(9n));
assertOptimized(f);

// Optmized asUintN deopts and throws TypeError.
assertThrows(() => f(2), TypeError);
assertUnoptimized(f);

%OptimizeFunctionOnNextCall(f);
assertEquals(7n, f(7n));
assertOptimized(f);

// Re-optimized still throws but does not deopt-loop.
assertThrows(() => f(2), TypeError);
assertOptimized(f);
