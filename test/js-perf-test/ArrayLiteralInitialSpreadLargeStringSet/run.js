// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Comparing different iterating schemes against spread initial literals.
// Benchmarks for large string set.

var keys = Array.from(Array(1e4).keys());
var set = new Set(keys.map((value) => 'val'+ value));

// ----------------------------------------------------------------------------
// Benchmark: Spread
// ----------------------------------------------------------------------------

function Spread() {
  var newArr = [...set];
  // basic sanity check
  if (newArr.length != set.size) throw 666;
  return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: Iterate
// ----------------------------------------------------------------------------

function Iterate() {
  var iterator = set[Symbol.iterator]();
  var length = set.size;
  var newArr = new Array(length);
  for (var i = 0; i < length; i++) {
    newArr[i] = iterator.next().value;
  }
  return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: Keys
// ----------------------------------------------------------------------------

function Keys() {
  var iterator = set.keys();
  var newArr = new Array(set.size);
  for (var i = 0; ; i++) {
    var iterResult = iterator.next();
    if (iterResult.done) break;
    newArr[i] = iterResult.value;
  }
  if (newArr.length != set.size) throw 666;
  return newArr;
}


// ----------------------------------------------------------------------------
// Setup and Run
// ----------------------------------------------------------------------------

load('../base.js');

var success = true;

function PrintResult(name, result) {
  print(name + '-ArrayLiteralInitialSpreadLargeStringSet(Score): ' + result);
}

function PrintError(name, error) {
  PrintResult('Error: ' + name, error);
  success = false;
}

// Run the benchmark (5 x 100) iterations instead of 1 second.
function CreateBenchmark(name, f) {
  new BenchmarkSuite(name, [1000], [ new Benchmark(name, false, false, 5, f) ]);
}

CreateBenchmark('Keys', Keys);
CreateBenchmark('Iterate', Iterate);
CreateBenchmark('Spread', Spread);


BenchmarkSuite.config.doWarmup = true;
BenchmarkSuite.config.doDeterministic = true;
BenchmarkSuite.RunSuites({NotifyResult: PrintResult, NotifyError: PrintError});
