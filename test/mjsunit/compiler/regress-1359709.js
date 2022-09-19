// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests exercise WebIDL annotations support in the fast API.

// Flags: --turbo-fast-api-calls --expose-fast-api --turbofan
// Flags: --fuzzing

const fast_c_api = new d8.test.FastCAPI();
function foo() {
    fast_c_api.test_wasm_memory(9638964294967297, 406934);
}
%PrepareFunctionForOptimization(foo);
try {
    foo();
} catch (e) {}
%OptimizeFunctionOnNextCall(foo);
foo();
