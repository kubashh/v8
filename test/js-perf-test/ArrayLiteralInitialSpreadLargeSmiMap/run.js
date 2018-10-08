// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Comparing different iterating schemes against spread initial literals.
// Benchmarks for large smi maps.

var keys = Array.from(Array(1e4).keys());
var keyValuePairs = keys.map((value) => [value, value + 1] );
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
// Benchmark: IterateWithLength
// ----------------------------------------------------------------------------

function IterateWithLength() {
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
  var newArr = new Array(map.size);
  for (var i = 0; ; i++) {
    var iteratorResult = iterator.next();
    if (iteratorResult.done) break;
    newArr[i] = iteratorResult.value;
  }
  if (newArr.length != map.size) throw 666;
  return newArr;
}


// ----------------------------------------------------------------------------
// Benchmark: Keys
// ----------------------------------------------------------------------------

function Keys() {
  var iterator = map.keys();
  var newArr = new Array(map.size);
  for (var i = 0; ; i++) {
    var iterResult = iterator.next();
    if (iterResult.done) break;
    newArr[i] = [iterResult.value, map.get(iterResult.value)];
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
  print(name + '-ArrayLiteralInitialSpreadLargeSmiMap(Score): ' + result);
}

function PrintError(name, error) {
  PrintResult('Error: ' + name, error);
  success = false;
}

// Run the benchmark (5 x 100) iterations instead of 1 second.
function CreateBenchmark(name, f) {
  new BenchmarkSuite(name, [1000], [ new Benchmark(name, false, false, 5, f) ]);
}

CreateBenchmark('Entries', Entries);
CreateBenchmark('Keys', Keys);
CreateBenchmark('IterateWithLength', IterateWithLength);
CreateBenchmark('Spread', Spread);


BenchmarkSuite.config.doWarmup = true;
BenchmarkSuite.config.doDeterministic = true;
BenchmarkSuite.RunSuites({NotifyResult: PrintResult, NotifyError: PrintError});
