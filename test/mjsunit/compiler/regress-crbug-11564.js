// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function f() {
  async function g(replace_me) {}
  %PrepareFunctionForOptimization(g);
  %OptimizeFunctionOnNextCall(g);
  new Promise(g);
}

const args = new Array(35000).fill('arg');
const f_async_many_args = f.toLocaleString().replace('replace_me', args);
eval(f_async_many_args);
f();
