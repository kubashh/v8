// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file excercises sequences support for  fast API calls.

// Flags: --turbo-fast-api-calls --allow-natives-syntax --opt
// --always-opt is disabled because we rely on particular feedback for
// optimizing to the fastest path.
// Flags: --no-always-opt
// The test relies on optimizing/deoptimizing at predictable moments, so
// it's not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0

const fast_c_api = new d8.test.FastCAPI();

// ----------- add_all_sequence -----------
// `add_all_sequence` has the following signature:
// double add_all_sequence(bool /*should_fallback*/, Local<Array>)

const max_safe_float = 2**24 - 1;
const add_all_result_full = -42 + 45 +
  Number.MIN_SAFE_INTEGER + Number.MAX_SAFE_INTEGER +
  max_safe_float * 0.5 + Math.PI;
const full_array = [-42, 45,
  Number.MIN_SAFE_INTEGER, Number.MAX_SAFE_INTEGER,
  max_safe_float * 0.5, Math.PI];

function add_all_sequence_smi(arg) {
  return fast_c_api.add_all_sequence(false /* should_fallback */, arg);
}

%PrepareFunctionForOptimization(add_all_sequence_smi);
assertEquals(3, add_all_sequence_smi([-42, 45]));
%OptimizeFunctionOnNextCall(add_all_sequence_smi);

function add_all_sequence_full(arg) {
  return fast_c_api.add_all_sequence(false /* should_fallback */, arg);
}

%PrepareFunctionForOptimization(add_all_sequence_full);
if (fast_c_api.supports_fp_params) {
  assertEquals(add_all_result_full, add_all_sequence_full(full_array));
} else {
  assertEquals(3, add_all_sequence_smi([-42, 45]));
}
%OptimizeFunctionOnNextCall(add_all_sequence_full);

if (fast_c_api.supports_fp_params) {
  // Test that regular call hits the fast path.
  fast_c_api.reset_counts();
  assertEquals(add_all_result_full, add_all_sequence_full(full_array));
  assertOptimized(add_all_sequence_full);
  assertEquals(1, fast_c_api.fast_call_count());
  assertEquals(0, fast_c_api.slow_call_count());
} else {
  // Smi only test - regular call hits the fast path.
  fast_c_api.reset_counts();
  assertEquals(3, add_all_sequence_smi([-42, 45]));
  assertOptimized(add_all_sequence_smi);
  assertEquals(1, fast_c_api.fast_call_count());
  assertEquals(0, fast_c_api.slow_call_count());
}

function add_all_sequence_mismatch(arg) {
  return fast_c_api.add_all_sequence(false /*should_fallback*/, arg);
}

%PrepareFunctionForOptimization(add_all_sequence_mismatch);
add_all_sequence_mismatch();
//assertThrows(() => add_all_sequence_mismatch());
%OptimizeFunctionOnNextCall(add_all_sequence_mismatch);

// Test that passing non-array arguments falls down the slow path.
fast_c_api.reset_counts();
assertThrows(() => add_all_sequence_mismatch(42));
assertOptimized(add_all_sequence_mismatch);
assertEquals(0, fast_c_api.fast_call_count());
assertEquals(1, fast_c_api.slow_call_count());

fast_c_api.reset_counts();
assertThrows(() => add_all_sequence_mismatch({}));
assertOptimized(add_all_sequence_mismatch);
assertEquals(0, fast_c_api.fast_call_count());
assertEquals(1, fast_c_api.slow_call_count());

fast_c_api.reset_counts();
assertThrows(() => add_all_sequence_mismatch('string'));
assertOptimized(add_all_sequence_mismatch);
assertEquals(0, fast_c_api.fast_call_count());
assertEquals(1, fast_c_api.slow_call_count());

fast_c_api.reset_counts();
assertThrows(() => add_all_sequence_mismatch(Symbol()));
assertOptimized(add_all_sequence_mismatch);
assertEquals(0, fast_c_api.fast_call_count());
assertEquals(1, fast_c_api.slow_call_count());


//----------- Test function overloads with same arity. -----------
//Only overloads between JSArray and TypedArray are supported

// Test with TypedArray.
(function () {
  function overloaded_test(should_fallback = false) {
    let typed_array = new Uint32Array([1, 2, 3]);
    return fast_c_api.add_all_overload(false /* should_fallback */,
        typed_array);
  }

  %PrepareFunctionForOptimization(overloaded_test);
  let result = overloaded_test();
  assertEquals(6, result);

  fast_c_api.reset_counts();
  %OptimizeFunctionOnNextCall(overloaded_test);
  result = overloaded_test();
  assertEquals(6, result);
  assertOptimized(overloaded_test);
  assertEquals(1, fast_c_api.fast_call_count());
})();

// Mismatched TypedArray.
(function () {
  function overloaded_test(should_fallback = false) {
    let typed_array = new Float32Array([1.1, 2.2, 3.3]);
    return fast_c_api.add_all_overload(false /* should_fallback */,
        typed_array);
  }

  %PrepareFunctionForOptimization(overloaded_test);
  let result = overloaded_test();
  assertEquals(0, result);

  fast_c_api.reset_counts();
  %OptimizeFunctionOnNextCall(overloaded_test);
  result = overloaded_test();
  assertEquals(0, result);
  assertOptimized(overloaded_test);
  assertEquals(0, fast_c_api.fast_call_count());
})();

// Test with JSArray.
(function () {
  let js_array = [1.1, 2.2, 3.3];
  let expected = 6.6;
  if (!fast_c_api.supports_fp_params) {
    js_array = [1, 2, 3];
    expected = 6;
  }
  function overloaded_test(should_fallback = false) {
    return fast_c_api.add_all_overload(false /* should_fallback */, js_array);
  }

  %PrepareFunctionForOptimization(overloaded_test);
  let result = overloaded_test();
  assertEquals(expected, result);

  fast_c_api.reset_counts();
  %OptimizeFunctionOnNextCall(overloaded_test);
  result = overloaded_test();
  assertEquals(expected, result);
  assertOptimized(overloaded_test);
  assertEquals(1, fast_c_api.fast_call_count());
})();

// Test function overloads with undefined.
(function () {
  function overloaded_test(should_fallback = false) {
    return fast_c_api.add_all_overload(false /* should_fallback */, undefined);
  }

  %PrepareFunctionForOptimization(overloaded_test);
  result = overloaded_test();
  assertEquals(0, result);

  fast_c_api.reset_counts();
  %OptimizeFunctionOnNextCall(overloaded_test);
  result = overloaded_test();
  assertEquals(0, result);
  assertOptimized(overloaded_test);
  assertEquals(0, fast_c_api.fast_call_count());
})();

// Test function overloads with invalid overloads.
(function () {
  function overloaded_test(should_fallback = false) {
    return fast_c_api.add_all_invalid_overload(false /* should_fallback */,
        [1, 2, 3]);
  }

  %PrepareFunctionForOptimization(overloaded_test);
  result = overloaded_test();
  assertEquals(6, result);

  fast_c_api.reset_counts();
  %OptimizeFunctionOnNextCall(overloaded_test);
  result = overloaded_test();
  assertEquals(6, result);
  assertOptimized(overloaded_test);
  assertEquals(0, fast_c_api.fast_call_count());
})();

//----------- Test different TypedArray functions. -----------
// ----------- add_all_<TYPE>_typed_array -----------
// `add_all_<TYPE>_typed_array` have the following signature:
// double add_all_<TYPE>_typed_array(bool /*should_fallback*/, FastApiTypedArray<TYPE>)

(function () {
  function int32_test(should_fallback = false) {
    let typed_array = new Int32Array([-42, 1, 2, 3]);
    return fast_c_api.add_all_int32_typed_array(false /* should_fallback */, typed_array);
  }

  %PrepareFunctionForOptimization(int32_test);
  let result = int32_test();
  assertEquals(-36, result);

  fast_c_api.reset_counts();
  %OptimizeFunctionOnNextCall(int32_test);
  result = int32_test();
  assertEquals(-36, result);
  assertOptimized(int32_test);
  assertEquals(1, fast_c_api.fast_call_count());
})();

(function () {
  function uint32_test(should_fallback = false) {
    let typed_array = new Uint32Array([1, 2, 3]);
    return fast_c_api.add_all_uint32_typed_array(false /* should_fallback */, typed_array);
  }

  %PrepareFunctionForOptimization(uint32_test);
  let result = uint32_test();
  assertEquals(6, result);

  fast_c_api.reset_counts();
  %OptimizeFunctionOnNextCall(uint32_test);
  result = uint32_test();
  assertEquals(6, result);
  assertOptimized(uint32_test);
  assertEquals(1, fast_c_api.fast_call_count());
})();

const max_safe_as_bigint = BigInt(Number.MAX_SAFE_INTEGER);
(function () {
  // This also implies 64-bit int support.
  if (fast_c_api.supports_fp_params) {
    function int64_test(should_fallback = false) {
      let typed_array = new BigInt64Array([-42n, 1n, max_safe_as_bigint]);
      return fast_c_api.add_all_int64_typed_array(false /* should_fallback */, typed_array);
    }
    const expected = BigInt.asIntN(64, -42n + 1n + max_safe_as_bigint);

    %PrepareFunctionForOptimization(int64_test);
    let result = int64_test();
    assertEquals(expected, BigInt(result));

    fast_c_api.reset_counts();
    %OptimizeFunctionOnNextCall(int64_test);
    result = int64_test();
    assertEquals(expected, BigInt(result));
    assertOptimized(int64_test);
    assertEquals(1, fast_c_api.fast_call_count());
  }
})();

(function () {
  // This also implies 64-bit int support.
  if (fast_c_api.supports_fp_params) {
    function uint64_test(should_fallback = false) {
      let typed_array = new BigUint64Array([max_safe_as_bigint, 1n, 2n]);
      return fast_c_api.add_all_uint64_typed_array(false /* should_fallback */, typed_array);
    }
    const expected = BigInt.asUintN(64, max_safe_as_bigint + 1n + 2n);

    %PrepareFunctionForOptimization(uint64_test);
    let result = uint64_test();
    assertEquals(expected, BigInt(result));

    fast_c_api.reset_counts();
    %OptimizeFunctionOnNextCall(uint64_test);
    result = uint64_test();
    assertEquals(expected, BigInt(result));
    assertOptimized(uint64_test);
    assertEquals(1, fast_c_api.fast_call_count());
  }
})();
