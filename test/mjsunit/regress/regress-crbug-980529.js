// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt --no-always-opt

function v1(a) {
  try {
    a[0](a);
  } catch (e) {
    return "Nothing";
  }
}

const default_parameters = [print, "foo", "bar", 42];

%PrepareFunctionForOptimization(v1);
v1(default_parameters);
v1(default_parameters);
%OptimizeFunctionOnNextCall(v1);
v1(default_parameters);

assertOptimized(v1);

// Using a lambda that calls {v1} with an empty argument
// list will cause a deoptimization with {print} on the
// call stack, and throw an exception as a[0] is not callable.
Object.__proto__.toString = () => v1([]);
v1(default_parameters);

assertUnoptimized(v1);

// As an additional sanity check, make sure that the output
// matches with what we expect.
assertEquals("Nothing,foo,bar,42", default_parameters.toString());
