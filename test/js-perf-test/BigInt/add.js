// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

load('bigint-util.js');

const RNG_SEED = 952302;
const TEST_ITERATIONS = 1000;
const SLOW_TEST_ITERATIONS = 50;
const DIGIT_CASES = [1, 2, 4, 8, 16, 32, 64, 128]

let a = 0n;
let b = 0n;

// This dummy ensures that the feedback for benchmark.run() in the Measure function
// from base.js is not monomorphic, thereby preventing the benchmarks below from being inlined.
// This ensures consistent behavior and comparable results.
new BenchmarkSuite('Prevent-Inline-Dummy', [10000], [
  new Benchmark('Prevent-Inline-Dummy', true, false, 0, () => {})
]);


new BenchmarkSuite('Add-TypeError', [10000], [
  new Benchmark('Add-TypeError', true, false, 0, TestAddTypeError, SetUpTestAddTypeError)
]);


new BenchmarkSuite('Add-Zero', [1000], [
  new Benchmark('Add-Zero', true, false, 0, TestAddZero, SetUpTestAddZero)
]);


DIGIT_CASES.forEach((d) => {
  new BenchmarkSuite(`Add-SameSign-${d}`, [1000], [
    new Benchmark(`Add-SameSign-${d}`, true, false, 0, TestAddSameSign, () => SetUpTestAddSameSign(d))
  ]);
});


DIGIT_CASES.forEach((d) => {
  new BenchmarkSuite(`Add-DifferentSign-${d}`, [1000], [
    new Benchmark(`Add-DifferentSign-${d}`, true, false, 0, TestAddDifferentSign, () => SetUpTestAddDifferentSign(d))
  ]);
});


function SetUpTestAddTypeError() {
  let rng = new Rng(RNG_SEED);
  a = SmallRandomBigIntWithDigits(16, false, rng);
}

function TestAddTypeError() {
  let sum = a;
  for (let i = 0; i < SLOW_TEST_ITERATIONS; ++i) {
    try {
      sum = 0 + sum;
    }
    catch(e) {
    }
  }
  return sum;
}


function SetUpTestAddZero() {
  let rng = new Rng(RNG_SEED);
  a = SmallRandomBigIntWithDigits(16, false, rng);
}

function TestAddZero() {
  let sum = a;

  for (let i = 0; i < TEST_ITERATIONS; ++i) {
    sum = 0n + sum;
  }

  return sum;
}


function SetUpTestAddSameSign(digits) {
  let rng = new Rng(RNG_SEED);
  a = SmallRandomBigIntWithDigits(digits, false, rng);
  b = SmallRandomBigIntWithDigits(digits, false, rng);
}

function TestAddSameSign() {
  let sum = b;

  for (let i = 0; i < TEST_ITERATIONS; ++i) {
    sum = a + sum;
  }

  return sum;
}


function SetUpTestAddDifferentSign(digits) {
  let rng = new Rng(RNG_SEED);
  a = SmallRandomBigIntWithDigits(digits, true, rng);
  b = MaxBigIntWithDigits(digits, false);
}

function TestAddDifferentSign() {
  let sum = b;

  for (let i = 0; i < TEST_ITERATIONS; ++i) {
    sum = a + sum;
  }

  return sum;
}
