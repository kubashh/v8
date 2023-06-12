// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --maglev

function maybe_throw() {}
function foo() {
  var x;
  var inner = function () {
    // This modifies the statically-known parent context.
    x = "foo";
  };
  %PrepareFunctionForOptimization(inner);

  try {
    // Call a function so that we don't eliminate the try-catch.
    maybe_throw();
  } catch (e) {}

  // After this try-catch, we should not have a Phi for the context, else
  // context load-after-store elimination will incorrectly miss the store inside
  // `inner`.

  x = "bar"; // store context[x] = "bar"
  inner();   // store context[x] = "foo" via inner
  return x;  // load context[x]
};

%NeverOptimizeFunction(maybe_throw);
%PrepareFunctionForOptimization(foo);
assertEquals("foo", foo());
assertEquals("foo", foo());
%OptimizeMaglevOnNextCall(foo);
assertEquals("foo", foo());
