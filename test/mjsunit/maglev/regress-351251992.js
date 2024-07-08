// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

(function () {
  function foo() {
    var __v_54 = 0;
    __v_54++;
    String.prototype[Symbol.iterator].call(__v_54);
  }
  %PrepareFunctionForOptimization(foo);
  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
})();

(function () {
  function foo() {
    const v3 = new Int16Array(16);
    for (let v4 of v3) {
      v4--;
      ("getDate")[Symbol.iterator].call(v4);
    }
  }
  %PrepareFunctionForOptimization(foo);
  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
})();
