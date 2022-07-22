// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev --no-stress-opt --no-always-turbofan

function foo(x) {
  return x ?? 10
}

%PrepareFunctionForOptimization(foo);
assertEquals(10, foo(undefined));
assertEquals(10, foo(undefined));
%OptimizeMaglevOnNextCall(foo);
assertEquals(10, foo(undefined));
assertTrue(isMaglevved(foo));
assertEquals(1, foo(1));
assertEquals(2, foo(2));
assertUnoptimized(foo);

%PrepareFunctionForOptimization(foo);
assertEquals(1, foo(1));
%OptimizeMaglevOnNextCall(foo);
assertEquals(2, foo(2));
assertEquals(3, foo(3));
assertTrue(isMaglevved(foo));
assertEquals(10, foo(undefined));
assertEquals(10, foo(null));
assertUnoptimized(foo);
