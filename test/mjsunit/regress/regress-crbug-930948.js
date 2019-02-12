// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --verify-heap --allow-natives-syntax

// This checks that TransitionAndStoreNumberElement silences NaNs.
function foo(x) {
  return [x].map(x => Math.asin(x));
}
foo(undefined);
foo(undefined);
%OptimizeFunctionOnNextCall(foo);
foo(undefined);

// This checks that TransitionAndStoreElement silences NaNs.
function bar(x, b) {
  return [x].map(x => b ? Math.asin(x) : "string");
}
bar(undefined, true);
bar(undefined, false);
bar(undefined, true);
bar(undefined, false);
%OptimizeFunctionOnNextCall(bar);
bar(undefined, true);
