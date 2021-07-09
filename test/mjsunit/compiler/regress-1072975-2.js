// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt


var arr = new Int8Array(2**32 - 1);


(function TestWithInBoundsFeedback() {
  function foo(a, i) {
    return a[i];
  }

  %PrepareFunctionForOptimization(foo);
  assertSame(0, foo(arr, 0));
  %OptimizeFunctionOnNextCall(foo);
  assertSame(0, foo(arr, 0));
  assertSame(0, foo(arr, -0));
  assertSame(0, foo(arr, 42));
  assertSame(0, foo(arr, 2**32 - 2));
  assertOptimized(foo);

  assertSame(undefined, foo(arr, 2**32 - 1));
  assertSame(undefined, foo(arr, 2**53 - 1));
  assertOptimized(foo);

  assertSame(undefined, foo(arr, 2**53));
  assertUnoptimized(foo);

  %PrepareFunctionForOptimization(foo);
  assertSame(undefined, foo(arr, 2**53));
  %OptimizeFunctionOnNextCall(foo);
  assertSame(undefined, foo(arr, 2**53));
  assertOptimized(foo);
})();

(function TestWithOutOfBoundsFeedback() {
  function foo(a, i) {
    return a[i];
  }

  %PrepareFunctionForOptimization(foo);
  assertSame(undefined, foo(arr, 2**32 - 1));
  %OptimizeFunctionOnNextCall(foo);
  assertSame(0, foo(arr, 0));
  assertSame(0, foo(arr, -0));
  assertSame(0, foo(arr, 42));
  assertSame(0, foo(arr, 2**32 - 2));
  assertOptimized(foo);

  assertSame(undefined, foo(arr, 2**32 - 1));
  assertSame(undefined, foo(arr, 2**53 - 1));
  assertOptimized(foo);

  assertSame(undefined, foo(arr, 2**53));
  assertUnoptimized(foo);

  %PrepareFunctionForOptimization(foo);
  assertSame(undefined, foo(arr, 2**53));
  %OptimizeFunctionOnNextCall(foo);
  assertSame(undefined, foo(arr, 2**53));
  assertOptimized(foo);
})();

(function() {
  function foo(a, i) {
    return a[i];
  }
  %PrepareFunctionForOptimization(foo);
  assertSame(0, foo(arr, -0));
  %OptimizeFunctionOnNextCall(foo);
  assertSame(0, foo(arr, -0));
  assertOptimized(foo);
})();


(function() {
  function foo(a, i) {
    return a[i];
  }

  %PrepareFunctionForOptimization(foo);
  assertSame(0, foo(arr, 42));
  %OptimizeFunctionOnNextCall(foo);
  assertSame(undefined, foo(arr, -(2**53)));
  assertUnoptimized(foo);

  %PrepareFunctionForOptimization(foo);
  assertSame(undefined, foo(arr, -(2**53)));
  %OptimizeFunctionOnNextCall(foo);
  assertSame(undefined, foo(arr, -(2**53)));
  assertOptimized(foo);
})();


(function() {
  function foo(a, i) {
    return a[i];
  }
  %PrepareFunctionForOptimization(foo);
  assertSame(undefined, foo(arr, -(2**53 - 1)));
  %OptimizeFunctionOnNextCall(foo);
  assertSame(undefined, foo(arr, -(2**53 - 1)));
  assertUnoptimized(foo);  // XXX This is a deopt loop.
})();

(function() {
  function foo(a, i) {
    return a[i];
  }
  %PrepareFunctionForOptimization(foo);
  assertSame(undefined, foo(arr, -42));
  %OptimizeFunctionOnNextCall(foo);
  assertSame(undefined, foo(arr, -42));
  assertUnoptimized(foo);  // XXX This is a deopt loop.
})();
