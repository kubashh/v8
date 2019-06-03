// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

load('bigint-util.js');

const RNG_SEED = 952302;
const TEST_ITERATIONS = 1000;
const DIGIT_CASES = [1, 2, 4, 8, 16, 32, 64, 128]

// This dummy ensures that the feedback for benchmark.run() in the Measure function
// from base.js is not monomorphic, thereby preventing the benchmarks below from being inlined.
// This ensures consistent behavior and comparable results.
new BenchmarkSuite('Prevent-Inline-Dummy', [10000], [
  new Benchmark('Prevent-Inline-Dummy', true, false, 0, () => {})
]);


new BenchmarkSuite('Add-TypeError', [10000], [
  new Benchmark('Add-TypeError', true, false, 0, () => TestAddTypeError())
]);


new BenchmarkSuite('Add-Zero', [1000], [
  new Benchmark('Add-Zero', true, false, 0, () => TestAddZero())
]);


(function(digits) {
  digits.forEach((d) => {
    new BenchmarkSuite(`Add-SameSign-${d}`, [1000], [
      new Benchmark(`Add-SameSign-${d}`, true, false, 0, () => TestAddSameSign(d))
    ]);
  })
})(DIGIT_CASES);


(function(digits) {
  digits.forEach((d) => {
    new BenchmarkSuite(`Add-DifferentSign-${d}`, [1000], [
      new Benchmark(`Add-DifferentSign-${d}`, true, false, 0, () => TestAddDifferentSign(d))
    ]);
  });
})(DIGIT_CASES);


function TestAddTypeError() {
  let sum = 42n;
  for (let i = 0; i < TEST_ITERATIONS; ++i) {
    try {
      sum = 0 + sum;
    }
    catch(e) {
    }
  }
  return sum;
}


function TestAddZero() {
  let rng = new Rng(RNG_SEED);
  let sum = SmallRandomBigIntWithDigits(16, false, rng);

  for (let i = 0; i < TEST_ITERATIONS; ++i) {
    sum = 0n + sum;
  }

  return sum;
}


function TestAddSameSign(digits) {
  let rng = new Rng(RNG_SEED);
  let sum = SmallRandomBigIntWithDigits(digits, false, rng);
  let a = SmallRandomBigIntWithDigits(digits, false, rng);

  for (let i = 0; i < TEST_ITERATIONS; ++i) {
    sum = a + sum;
  }

  return sum;
}


function TestAddDifferentSign(digits) {
  let rng = new Rng(RNG_SEED);
  let sum = MaxBigIntWithDigits(digits, false);
  let a = SmallRandomBigIntWithDigits(digits, true, rng);

  for (let i = 0; i < TEST_ITERATIONS; ++i) {
    sum = a + sum;
  }

  return sum;
}
