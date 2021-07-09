// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt

function create(n) {
  return new Uint32Array(
      new SharedArrayBuffer(Uint32Array.BYTES_PER_ELEMENT * n));
}

function foo(n, i) {
  return create(n)[i];
}

%NeverOptimizeFunction(create);
%PrepareFunctionForOptimization(foo);
assertSame(undefined, foo(10, 100));
%OptimizeFunctionOnNextCall(foo);
assertSame(undefined, foo(10, 100));
assertOptimized(foo);
assertSame(undefined, foo(2**32 - 1, -10));
assertOptimized(foo);
