// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function() {

function ThrowAndCatch(fn) {
  try {
    fn();
  } catch (err) {
    // Do nothing. The benchmark measures the stack-walking needed
    // to create Error objects.
  }
}

function Simple() {
  ThrowAndCatch(() => { throw new Error("Simple Error"); });
}

class CustomError extends Error {};
function Custom() {
  ThrowAndCatch(() => { throw new CustomError("Custom Error"); });
}

function Inline() {
  function Inner() {
    throw new Error("Throwing from inlined function!");
  }
  function Middle() { Inner(); }
  function Outer() { Middle(); }

  try { Outer(); } catch(err) {}
  try { Outer(); } catch(err) {}
  %OptimizeFunctionOnNextCall(Outer);
  try { Outer(); } catch(err) {}

  ThrowAndCatch(() => Outer());
}

createSuite('Simple-Capture-Error', 1000, Simple, () => {});
createSuite('Custom-Capture-Error', 1000, Custom, () => {});

createSuite('Inline-Capture-Error', 1000, Inline, () => {});

})();
