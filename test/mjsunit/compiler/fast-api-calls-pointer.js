// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file excercises basic fast API calls and enables fuzzing of this
// functionality.

// Flags: --turbo-fast-api-calls --expose-fast-api --allow-natives-syntax --turbofan
// --always-turbofan is disabled because we rely on particular feedback for
// optimizing to the fastest path.
// Flags: --no-always-turbofan
// The test relies on optimizing/deoptimizing at predictable moments, so
// it's not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0

assertThrows(() => d8.test.FastCAPI());
const fast_c_api = new d8.test.FastCAPI();

// ---------- Test external pointer passing -----------

function assertIsExternal(pointer) {
  return fast_c_api.assert_is_external(pointer);
}

function get_pointer_a() {
  return fast_c_api.get_pointer();
}

function get_pointer_b() {
  return fast_c_api.get_other_pointer();
}

function pass_pointer(pointer) {
  return fast_c_api.pass_pointer(pointer);
}

function compare_pointers(pointer_a, pointer_b) {
  return fast_c_api.compare_pointers(pointer_a, pointer_b);
}

%PrepareFunctionForOptimization(get_pointer_a);
const external_a_slow = get_pointer_a();
const external_a_slow_clone = get_pointer_a();
assertUnoptimized(get_pointer_a);
assertIsExternal(external_a_slow);
assertIsExternal(external_a_slow_clone);

// Slow call that returns the same pointer from a new `External::New()`
// will still create a new / different object.
// Note that we cannot use `assertEquals(external_a_slow, external_a_slow_clone)`
// as it's a deep equality comparison and will return true for all empty object comparsions.
assertEquals(external_a_slow === external_a_slow_clone, false);

%PrepareFunctionForOptimization(pass_pointer);
const external_a_slow_passed = pass_pointer(external_a_slow);
// If slow call returns the same External object, then object identity is
// preserved.
assertEquals(external_a_slow_passed === external_a_slow, true);
%OptimizeFunctionOnNextCall(pass_pointer);
const external_a_fast_passed = pass_pointer(external_a_slow);
assertOptimized(pass_pointer);
assertIsExternal(external_a_slow);
assertIsExternal(external_a_fast_passed);
// Fast call always creates a new External object, as they cannot
// return the same External object given that they do not see it.
assertEquals(external_a_fast_passed === external_a_slow, false);

// An object that looks like an External is still not an External.
const emptyObject = Object.create(null);
assertThrows(() => pass_pointer(emptyObject));

// Show off deep equality comparsions between various External objects and
// the empty object to show that all Externals work properly as objects.
assertEquals(external_a_slow, external_a_fast_passed);
assertEquals(external_a_fast_passed, emptyObject);

%OptimizeFunctionOnNextCall(get_pointer_a);
const external_a_fast = get_pointer_a();
assertOptimized(get_pointer_a);
assertIsExternal(external_a_fast);
assertEquals(external_a_fast === external_a_slow, false);

%PrepareFunctionForOptimization(get_pointer_b);
const external_b_slow = get_pointer_b();
assertUnoptimized(get_pointer_b);
assertEquals(external_b_slow, null);
%OptimizeFunctionOnNextCall(get_pointer_b);
const external_b_fast = get_pointer_b();
assertOptimized(get_pointer_b);
assertEquals(external_b_fast, null);

const external_b_fast_passed = pass_pointer(external_b_slow);
assertEquals(external_b_fast_passed, null);
assertEquals(external_b_fast_passed === external_b_slow, true);

%PrepareFunctionForOptimization(compare_pointers);
assertUnoptimized(compare_pointers);
assertEquals(compare_pointers(external_a_slow, external_b_slow), false);
%OptimizeFunctionOnNextCall(compare_pointers);
assertEquals(compare_pointers(external_a_slow, external_b_slow), false);
assertOptimized(compare_pointers);
assertEquals(compare_pointers(external_a_slow, external_a_slow), true);
assertEquals(compare_pointers(external_a_slow, external_a_fast), true);
assertEquals(compare_pointers(external_b_slow, external_b_slow), true);
assertEquals(compare_pointers(external_b_slow, external_b_fast), true);
assertEquals(compare_pointers(external_b_slow, external_b_fast_passed), true);
