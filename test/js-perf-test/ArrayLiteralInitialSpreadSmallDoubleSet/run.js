// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Comparing different iterating schemes against spread initial literals.
// Benchmarks for small double set.

var keys = Array.from(Array(50).keys());
var set = new Set(keys.map((value) => value + 6.66));

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
  var newArr = new Array(set.size);
  var i = 0;
  for (let kv of set) {
    newArr[i] = kv;
    i++;
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
  print(name + '-ArrayLiteralInitialSpreadSmallDoubleSet(Score): ' + result);
}

function PrintError(name, error) {
  PrintResult('Error: ' + name, error);
  success = false;
}

function CreateBenchmark(name, f) {
  new BenchmarkSuite(name, [1000], [ new Benchmark(name, false, false, 0, f) ]);
}

CreateBenchmark('Iterate', Iterate);
CreateBenchmark('Spread', Spread);


BenchmarkSuite.config.doWarmup = true;
BenchmarkSuite.config.doDeterministic = false;
BenchmarkSuite.RunSuites({NotifyResult: PrintResult, NotifyError: PrintError});
