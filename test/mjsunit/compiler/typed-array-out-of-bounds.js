// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --noalways-opt --opt

// TODO(turbofan): Add BigInt64Array and BigUint64Array here, once TurboFan
// supports those.
const TYPED_ARRAY_CONSTRUCTORS = [
    Int8Array,
    Uint8Array,
    Int16Array,
    Uint16Array,
    Int32Array,
    Uint32Array,
    Float32Array,
    Float64Array,
    Uint8ClampedArray
];

// Test proper out-of-bounds handling for reading from a TypedArray.
TYPED_ARRAY_CONSTRUCTORS.forEach(C => {
  const a = new C([42]);

  // We need to generate a new function for each constructor, otherwise
  // the feedback will be polluted in the shared function.
  const get = new Function("a", "i", `/* ${C.name} */ return a[i];`);

  // Test in-bounds accesses.
  %PrepareFunctionForOptimization(get);
  assertEquals(42, get(a, 0));
  assertEquals(42, get(a, 0));
  %OptimizeFunctionOnNextCall(get);
  assertEquals(42, get(a, 0));
  assertOptimized(get);

  // Accessing out-of-bounds (positive index) should fail bounds check.
  assertEquals(undefined, get(a, 1));
  assertUnoptimized(get);

  // Test out-of-bounds accesses.
  %OptimizeFunctionOnNextCall(get);
  assertEquals(undefined, get(a, 1));
  assertOptimized(get);

  // Accessing out-of-bounds (negative index) should fail bounds check.
  assertEquals(undefined, get(a, -1));
  assertUnoptimized(get);

  // Now the feedback should work for both negative and positive indices.
  %OptimizeFunctionOnNextCall(get);
  assertEquals(undefined, get(a, 1));
  assertEquals(undefined, get(a, -1));
  assertOptimized(get);
});

// Test proper out-of-bounds handling for writing to a TypedArray.
TYPED_ARRAY_CONSTRUCTORS.forEach(C => {
  const a = new C([42]);

  // We need to generate a new function for each constructor, otherwise
  // the feedback will be polluted in the shared function.
  const set = new Function("a", "i", "v", `/* ${C.name} */ a[i] = v;`);

  // Test in-bounds accesses.
  %PrepareFunctionForOptimization(set);
  set(a, 0, 42);
  set(a, 0, 42);
  %OptimizeFunctionOnNextCall(set);
  set(a, 0, 42);
  assertOptimized(set);

  // Accessing out-of-bounds (positive index) should fail bounds check.
  set(a, 1, 0);
  assertEquals(undefined, a[1]);
  assertUnoptimized(set);

  // Test out-of-bounds accesses.
  %OptimizeFunctionOnNextCall(set);
  set(a, 1, 0);
  assertEquals(undefined, a[1]);
  assertOptimized(set);

  // Accessing out-of-bounds (negative index) should fail bounds check.
  set(a, -1, 0);
  assertEquals(undefined, a[-1]);
  assertUnoptimized(set);

  // Now the feedback should work for both negative and positive indices.
  %OptimizeFunctionOnNextCall(set);
  set(a, 1, 0);
  assertEquals(undefined, a[1]);
  set(a, -1, 0);
  assertEquals(undefined, a[-1]);
  assertOptimized(set);
});
