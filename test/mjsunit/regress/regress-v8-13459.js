// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function g(...x) {
  { var x; }
  function x(){}
  assertEquals('function', typeof x);
};
g(0);

function f(w, ...x){
  function x(){}
  { var x; }
  assertEquals('function', typeof x);
};
f(0, 0);
