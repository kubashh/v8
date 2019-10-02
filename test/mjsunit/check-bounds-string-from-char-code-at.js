// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --opt

let notAnArrayIndex = false;
let string = "foobar";

function f() {
  let index;
  if (notAnArrayIndex) {
    index = '4294967296';
  } else {
    index = '1';
  }
  return string.charCodeAt(index);
}

%PrepareFunctionForOptimization(f);
f();
f();

%OptimizeFunctionOnNextCall(f);
// causes deopt
notAnArrayIndex = true;
f();

%PrepareFunctionForOptimization(f);
notAnArrayIndex = false;
f();
f();

%OptimizeFunctionOnNextCall(f);
f();

// no deopt here
notAnArrayIndex = true;
f();
assertOptimized(f);
