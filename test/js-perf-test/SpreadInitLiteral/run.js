// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Different copy schemes against spread initial literals
var smallHoleyArray = Array(100);
const smallArray = Array.from(Array(100).keys());

for (var i = 0; i < 10; i++) {
  smallHoleyArray[i] = i;
}
for (var i = 90; i < 99; i++) {
  smallHoleyArray[i] = i;
}

var largeHoleyArray = new Array(100000);
const largeArray = Array.from(largeHoleyArray.keys());

for (var i = 0; i < 100; i++) {
  largeHoleyArray[i] = i;
}

for (var i = 5000; i < 5500; i++) {
  largeHoleyArray[i] = i;
}

// ----------------------------------------------------------------------------
// Benchmark: Spread
// ----------------------------------------------------------------------------

function Spread(arr) {
  var newArr = [...arr];
  // basic sanity check
  if (newArr.length != arr.length) throw 666;
  // return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: ForLength
// ----------------------------------------------------------------------------


function ForLength(arr) {
  var newArr = new Array(arr.length);
  for (let i = 0; i < arr.length; i++) {
    newArr[i] = arr[i];
  }
  // basic sanity check
  if (newArr.length != arr.length) throw 666;
  // return newArr;
}


// ----------------------------------------------------------------------------
// Benchmark: ForLengthEmpty
// ----------------------------------------------------------------------------


function ForLengthEmpty(arr) {
  var newArr = [];
  for (let i = 0; i < arr.length; i++) {
    newArr[i] = arr[i];
  }
  // basic sanity check
  if (newArr.length != arr.length) throw 666;
  // return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: Slice
// ----------------------------------------------------------------------------


function Slice(arr) {
  var newArr = arr.slice();
  // basic sanity check
  if (newArr.length != arr.length) throw 666;
  // return newArr;
}


// ----------------------------------------------------------------------------
// Benchmark: Slice0
// ----------------------------------------------------------------------------


function Slice0(arr) {
  var newArr = arr.slice(0);
  // basic sanity check
  if (newArr.length != arr.length) throw 666;
  // return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: ConcatBefore
// ----------------------------------------------------------------------------


function ConcatBefore(arr) {
  var newArr = arr.concat();
  // basic sanity check
  if (newArr.length != arr.length) throw 666;
  // return newArr;
}


// ----------------------------------------------------------------------------
// Benchmark: ConcatAfter
// ----------------------------------------------------------------------------


function ConcatAfter(arr) {
  var newArr = [].concat(arr);
  // basic sanity check
  if (newArr.length != arr.length) throw 666;
  // return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: ForOfPush
// ----------------------------------------------------------------------------


function ForOfPush(arr) {
  var newArr = [];
  for (let x of arr) {
    newArr.push(x)
  }
  // basic sanity check
  if (newArr.length != arr.length) throw 666;
  // return newArr;
}


// ----------------------------------------------------------------------------
// Setup and Run
// ----------------------------------------------------------------------------

load('../base.js');

var success = true;

function PrintResult(name, result) {
  print(name + '-SpreadInitLiteral(Score): ' + result);
}

function PrintError(name, error) {
  PrintResult(name, error);
  success = false;
}

function CreateBenchmark(name, f) {
  new BenchmarkSuite(name, [1000], [ new Benchmark(name, false, false, 0, f) ]);
}

CreateBenchmark('Spread-Small', () => Spread(smallArray));
CreateBenchmark('ForLength-Small', () => ForLength(smallArray));
CreateBenchmark('ForLengthEmpty-Small', () => ForLengthEmpty(smallArray));
CreateBenchmark('Slice-Small', () => Slice(smallArray));
CreateBenchmark('Slice0-Small', () => Slice0(smallArray));
CreateBenchmark('ConcatBefore-Small', () => ConcatBefore(smallArray));
CreateBenchmark('ConcatAfter-Small', () => ConcatAfter(smallArray));
CreateBenchmark('ForOfPush-Small', () => ForOfPush(smallArray));

CreateBenchmark('Spread-Large', () => Spread(largeArray));
CreateBenchmark('ForLength-Large', () => ForLength(largeArray));
CreateBenchmark('ForLengthEmpty-Large', () => ForLengthEmpty(largeArray));
CreateBenchmark('Slice-Large', () => Slice(largeArray));
CreateBenchmark('Slice0-Large', () => Slice0(largeArray));
CreateBenchmark('ConcatBefore-Large', () => ConcatBefore(largeArray));
CreateBenchmark('ConcatAfter-Large', () => ConcatAfter(largeArray));
CreateBenchmark('ForOfPush-Large', () => ForOfPush(largeArray));

CreateBenchmark('Spread-SmallHoley', () => Spread(smallHoleyArray));
CreateBenchmark('ForLength-SmallHoley', () => ForLength(smallHoleyArray));
CreateBenchmark('ForLengthEmpty-SmallHoley', () => ForLengthEmpty(smallHoleyArray));
CreateBenchmark('Slice-SmallHoley', () => Slice(smallHoleyArray));
CreateBenchmark('Slice0-SmallHoley', () => Slice0(smallHoleyArray));
CreateBenchmark('ConcatBefore-SmallHoley', () => ConcatBefore(smallHoleyArray));
CreateBenchmark('ConcatAfter-SmallHoley', () => ConcatAfter(smallHoleyArray));
CreateBenchmark('ForOfPush-SmallHoley', () => ForOfPush(smallHoleyArray));


CreateBenchmark('Spread-LargeHoley', () => Spread(largeHoleyArray));
CreateBenchmark('ForLength-LargeHoley', () => ForLength(largeHoleyArray));
CreateBenchmark('ForLengthEmpty-LargeHoley', () => ForLengthEmpty(largeHoleyArray));
CreateBenchmark('Slice-LargeHoley', () => Slice(largeHoleyArray));
CreateBenchmark('Slice0-LargeHoley', () => Slice0(largeHoleyArray));
CreateBenchmark('ConcatBefore-LargeHoley', () => ConcatBefore(largeHoleyArray));
CreateBenchmark('ConcatAfter-LargeHoley', () => ConcatAfter(largeHoleyArray));
CreateBenchmark('ForOfPush-LargeHoley', () => ForOfPush(largeHoleyArray));

BenchmarkSuite.config.doWarmup = undefined;
BenchmarkSuite.config.doDeterministic = undefined;
BenchmarkSuite.RunSuites({NotifyResult: PrintResult, NotifyError: PrintError});
