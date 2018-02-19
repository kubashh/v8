// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --branch-load-poisoning

// Make sure that --branch-load-poisoning works with exceptions.

function f() {
  try {
    throw {b: 0xa5a5a5, c: 0x5a5a5a};
  } catch (e) {
    // If the poison register isn't reset on entry to a catch, then the results
    // of loading from `e` will be masked with an unpredictable poison. We've
    // chosen values for `b` and `c` such that it is unlikely for the masking to
    // yield the correct result.
    return e.b | e.c;
  }
}

f();
f();
%OptimizeFunctionOnNextCall(f);
assertEquals(f(), 0xffffff);
