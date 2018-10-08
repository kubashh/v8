// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Comparing different iterating schemes against spread initial literals.
// Benchmarks for large string maps.

var keys = Array.from(Array(1e4).keys());
var keyValuePairs = keys.map((value) => ['k' + value, 'v' + value] );
var map = new Map(keyValuePairs);

// ----------------------------------------------------------------------------
// Benchmark: Spread
// ----------------------------------------------------------------------------

function Spread() {
  var newArr = [...map];
  // basic sanity check
  if (newArr.length != map.size) throw 666;
  return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: Iterate
// ----------------------------------------------------------------------------

function Iterate() {
  var newArr = new Array(map.size);
  var i = 0;
  for (let kv of map) {
    newArr[i] = kv;
    i++;
  }
  if (newArr.length != map.size) throw 666;
  return newArr;
}

// ----------------------------------------------------------------------------
// Setup and Run
// ----------------------------------------------------------------------------

load('../base.js');

var success = true;

function PrintResult(name, result) {
  print(name + '-ArrayLiteralInitialSpreadLargeStringMap(Score): ' + result);
}

function PrintError(name, error) {
  PrintResult('Error: ' + name, error);
  success = false;
}

// Run the benchmark (5 x 100) iterations instead of 1 second.
function CreateBenchmark(name, f) {
  new BenchmarkSuite(name, [1000], [ new Benchmark(name, false, false, 5, f) ]);
}

CreateBenchmark('Iterate', Iterate);
CreateBenchmark('Spread', Spread);


BenchmarkSuite.config.doWarmup = true;
BenchmarkSuite.config.doDeterministic = true;
BenchmarkSuite.RunSuites({NotifyResult: PrintResult, NotifyError: PrintError});
