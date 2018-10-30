// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function reduce() {
  for (var i = 0; i < 2 ;i++) {
    let [q, r] = [1n, 1n];
    r = r - 1n;
    q += 1n;
    q = r;
  }
}

reduce();
%OptimizeFunctionOnNextCall(reduce);
reduce();
