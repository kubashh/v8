// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

d8.file.execute('bigint-util.js');

let initial_product = 0n;
let a = 0n;
let random_bigints = [];

// This dummy ensures that the feedback for benchmark.run() in the Measure
// function from base.js is not monomorphic, thereby preventing the benchmarks
// below from being inlined. This ensures consistent behavior and comparable
// results.
new BenchmarkSuite('Prevent-Inline-Dummy', [10000], [
  new Benchmark('Prevent-Inline-Dummy', true, false, 0, () => {})
]);


new BenchmarkSuite('Multiply-TypeError', [10000], [
  new Benchmark('Multiply-TypeError', true, false, 0, TestMultiplyTypeError,
    SetUpTestMultiplyTypeError)
]);


new BenchmarkSuite('Multiply-Zero', [1000], [
  new Benchmark('Multiply-Zero', true, false, 0, TestMultiplyZero, SetUpTestMultiplyZero)
]);


BITS_CASES.forEach((d) => {
  new BenchmarkSuite(`Multiply-${d}`, [1000], [
    new Benchmark(`Multiply-${d}`, true, false, 0, TestMultiply,
      () => SetUpTestMultiply(d))
  ]);
});


new BenchmarkSuite('Multiply-Random', [1000], [
  new Benchmark('Multiply-Random', true, false, 0, TestMultiplyRandom,
    SetUpTestMultiplyRandom)
]);


function SetUpTestMultiplyTypeError() {
  initial_product = 42n;
}


function TestMultiplyTypeError() {
  let product = initial_product;
  for (let i = 0; i < SLOW_TEST_ITERATIONS; ++i) {
    try {
      product = 0 * product;
    }
    catch(e) {
    }
  }
  return product;
}


function SetUpTestMultiplyZero() {
  initial_product = 42n;
}


function TestMultiplyZero() {
  let product = initial_product;

  for (let i = 0; i < TEST_ITERATIONS; ++i) {
    product = 0n * product;
  }

  return product;
}


function SetUpTestMultiply(bits) {
  initial_product = SmallRandomBigIntWithBits(bits);
  a = SmallRandomBigIntWithBits(bits);
}


function TestMultiply() {
  let product = initial_product;

  for (let i = 0; i < TEST_ITERATIONS; ++i) {
    product = a * product;
  }

  return product;
}


function SetUpTestMultiplyRandom() {
  random_bigints = [];
  // RandomBigIntWithBits needs multiples of 4 bits.
  const max_in_4bits = RANDOM_BIGINTS_MAX_BITS / 4;
  for (let i = 0; i < TEST_ITERATIONS; ++i) {
    const bits = Math.floor(Math.random() * max_in_4bits) * 4;
    const bigint = RandomBigIntWithBits(bits);
    random_bigints.push(Math.random() < 0.5 ? -bigint : bigint);
  }
}


function TestMultiplyRandom() {
  let product = 0n;

  for (let i = 0; i < TEST_ITERATIONS; ++i) {
    product = random_bigints[i] * product;
  }

  return product;
}
