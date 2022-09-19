// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests exercise WebIDL annotations support in the fast API.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax --turbofan
// Flags: --fuzzing

const fast_c_api = new d8.test.FastCAPI();
function f() {
  try {
    fast_c_api.enforce_range_compare_u64(true, -1, 5e-324);
  } catch (e) {}
}

%PrepareFunctionForOptimization(f);
f();
%OptimizeFunctionOnNextCall(f);
f();
