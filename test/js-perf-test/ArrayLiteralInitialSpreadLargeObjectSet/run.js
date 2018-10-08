// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Comparing different iterating schemes against spread initial literals.
// Benchmarks for large object sets.

var keys = Array.from(Array(1e4).keys());
var set = new Set(keys.map((v) => {key : v; value: v+1}));

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
// Benchmark: IterateWithLength
// ----------------------------------------------------------------------------

function IterateWithLength() {
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
  print(name + '-ArrayLiteralInitialSpreadLargeObjectSet(Score): ' + result);
}

function PrintError(name, error) {
  PrintResult('Error: ' + name, error);
  success = false;
}

function CreateBenchmark(name, f) {
  new BenchmarkSuite(name, [100], [ new Benchmark(name, false, false, 0, f) ]);
}

CreateBenchmark('Keys', Keys);
CreateBenchmark('IterateWithLength', IterateWithLength);
CreateBenchmark('Spread', Spread);


BenchmarkSuite.config.doWarmup = true;
BenchmarkSuite.config.doDeterministic = false;
BenchmarkSuite.RunSuites({NotifyResult: PrintResult, NotifyError: PrintError});
