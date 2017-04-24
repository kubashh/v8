// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function addBenchmark(name, test) {
  new BenchmarkSuite(name, [1000],
      [
        new Benchmark(name, false, false, 0, test)
      ]);
}

addBenchmark('Smi-StrictEquals', SmiStrictEquals);
addBenchmark('Number-StrictEquals', NumberStrictEquals);
addBenchmark('String-StrictEquals', StringStrictEquals);
addBenchmark('SmiString-StrictEquals', MixedStrictEquals);
addBenchmark('Smi-Equals', SmiEquals);
addBenchmark('Number-Equals', NumberEquals);
addBenchmark('String-Equals', StringEquals);
addBenchmark('SmiString-Equals', MixedEquals);
addBenchmark('Smi-RelationalCompare', SmiRelationalCompare);
addBenchmark('Number-RelationalCompare', NumberRelationalCompare);
addBenchmark('String-RelationalCompare', StringRelationalCompare);
addBenchmark('SmiString-RelationalCompare', MixedRelationalCompare);

function strictEquals(a, b) {
  for (var i = 0; i < 1000; ++i) {
    a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b;
    a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b;
    a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b;
    a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b;
    a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b;
    a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b;
    a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b;
    a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b;
    a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b;
    a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b; a === b;
  }
}

function equals(a, b) {
  for (var i = 0; i < 1000; ++i) {
    a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b;
    a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b;
    a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b;
    a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b;
    a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b;
    a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b;
    a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b;
    a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b;
    a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b;
    a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b; a == b;
  }
}

// Relational comparison handlers are similar, so use one benchmark to measure
// all of them.
function relationalCompare(a, b) {
  for (var i = 0; i < 1000; ++i) {
    a > b; a > b; a > b; a > b; a > b; a > b; a > b; a > b; a > b; a > b;
    a > b; a > b; a > b; a > b; a > b; a > b; a > b; a > b; a > b; a > b;
    a > b; a > b; a > b; a > b; a > b; a > b; a > b; a > b; a > b; a > b;
    a >= b; a >= b; a >= b; a >= b; a >= b; a >= b; a >= b; a >= b; a >= b; a >= b;
    a >= b; a >= b; a >= b; a >= b; a >= b; a >= b; a >= b; a >= b; a >= b; a >= b;
    a >= b; a >= b; a >= b; a >= b; a >= b; a >= b; a >= b; a >= b; a >= b; a >= b;
    a < b; a < b; a < b; a < b; a < b; a < b; a < b; a < b; a < b; a < b;
    a < b; a < b; a < b; a < b; a < b; a < b; a < b; a < b; a < b; a < b;
    a < b; a < b; a < b; a < b; a < b; a < b; a < b; a < b; a < b; a < b;
    a <= b; a <= b; a <= b; a <= b; a <= b; a <= b; a <= b; a <= b; a <= b; a <= b;
    a <= b; a <= b; a <= b; a <= b; a <= b; a <= b; a <= b; a <= b; a <= b; a <= b;
    a <= b; a <= b; a <= b; a <= b; a <= b; a <= b; a <= b; a <= b; a <= b; a <= b;
  }
}

function SmiStrictEquals() {
 strictEquals(10, 20);
}

function NumberStrictEquals() {
 strictEquals(0.3333, 0.3334);
}

function StringStrictEquals() {
 strictEquals("abc", "def");
}

function MixedStrictEquals() {
 strictEquals(10, "10");
}

function SmiEquals() {
 strictEquals(10, 20);
}

function NumberEquals() {
 strictEquals(0.3333, 0.3333);
}

function StringEquals() {
 strictEquals("abc", "def");
}

function MixedEquals() {
 strictEquals(10, "10");
}

function SmiRelationalCompare() {
 relationalCompare(10, 20);
}

function NumberRelationalCompare() {
 relationalCompare(0.3333, 0.3334);
}

function StringRelationalCompare() {
 relationalCompare("abc", "def");
}

function MixedRelationalCompare() {
 relationalCompare(10, "10");
}
