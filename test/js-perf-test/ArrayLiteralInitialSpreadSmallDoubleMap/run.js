// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Comparing different iterating schemes against spread initial literals.
// Benchmarks for small double maps.

var keys = Array.from(Array(50).keys());
var keyValuePairs = keys.map((value) => [value + 0.666, value + 6.66] );
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
  var iterator = map[Symbol.iterator]();
  var length = map.size;
  var newArr = new Array(length);
  for (var i = 0; i < length; i++) {
    newArr[i] = iterator.next().value;
  }
  return newArr;
}


// ----------------------------------------------------------------------------
// Benchmark: Entries
// ----------------------------------------------------------------------------

function Entries() {
  var iterator = map.entries();
  var length = map.size;
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
  var iterator = map.keys();
  var length = map.size;
  var newArr = new Array(length);
  for (var i = 0; i < length; i++) {
    var iterResult = iterator.next();
    newArr[i] = [iterResult.value, map.get(iterResult.value)];
  }
  return newArr;
}


// ----------------------------------------------------------------------------
// Setup and Run
// ----------------------------------------------------------------------------

load('../base.js');

var success = true;

function PrintResult(name, result) {
  print(name + '-ArrayLiteralInitialSpreadSmallDoubleMap(Score): ' + result);
}

function PrintError(name, error) {
  PrintResult('Error: ' + name, error);
  success = false;
}

function CreateBenchmark(name, f) {
  new BenchmarkSuite(name, [1000], [ new Benchmark(name, false, false, 0, f) ]);
}

CreateBenchmark('Entries', Entries);
CreateBenchmark('Keys', Keys);
CreateBenchmark('Iterate', Iterate);
CreateBenchmark('Spread', Spread);


BenchmarkSuite.config.doWarmup = true;
BenchmarkSuite.config.doDeterministic = false;
BenchmarkSuite.RunSuites({NotifyResult: PrintResult, NotifyError: PrintError});
