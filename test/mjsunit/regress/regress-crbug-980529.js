// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt --no-always-opt

function v1() {
  const v9 = new Proxy(this, {});
  const v10 = Object.values(v9);
  try {
    v10[0](v10);
  } catch (e) {}
  return "Nothing";
}

%PrepareFunctionForOptimization(v1);
v1();
v1();
%OptimizeFunctionOnNextCall(v1);
v1();

assertOptimized(v1);

// Using {v1} as the global toString
// will cause a deoptimization with {print} on the
// call stack, and throw an exception as v10[0] is not callable.
Object.__proto__.toString = v1;
v1();

assertUnoptimized(v1);
