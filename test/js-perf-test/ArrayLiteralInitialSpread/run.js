// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Comparing different copy schemes against spread initial literals

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
  return () => {
      var newArr = [...arr];
      // basic sanity check
      if (newArr.length != arr.length) throw 666;
      return newArr;
    };
}

// ----------------------------------------------------------------------------
// Benchmark: ForLength
// ----------------------------------------------------------------------------


function ForLength(arr) {
  return () => {
      var newArr = new Array(arr.length);
      for (let i = 0; i < arr.length; i++) {
        newArr[i] = arr[i];
      }
      // basic sanity check
      if (newArr.length != arr.length) throw 666;
      return newArr;
    };
}


// ----------------------------------------------------------------------------
// Benchmark: ForLengthEmpty
// ----------------------------------------------------------------------------


function ForLengthEmpty(arr) {
  return () => {
      var newArr = [];
      for (let i = 0; i < arr.length; i++) {
        newArr[i] = arr[i];
      }
      // basic sanity check
      if (newArr.length != arr.length) throw 666;
      return newArr;
    };
}

// ----------------------------------------------------------------------------
// Benchmark: Slice
// ----------------------------------------------------------------------------


function Slice(arr) {
  return () => {
      var newArr = arr.slice();
      // basic sanity check
      if (newArr.length != arr.length) throw 666;
      return newArr;
    };
}


// ----------------------------------------------------------------------------
// Benchmark: Slice0
// ----------------------------------------------------------------------------


function Slice0(arr) {
  return () => {
      var newArr = arr.slice(0);
      // basic sanity check
      if (newArr.length != arr.length) throw 666;
      return newArr;
    };
}

// ----------------------------------------------------------------------------
// Benchmark: ConcatReceive
// ----------------------------------------------------------------------------


function ConcatReceive(arr) {
  return () => {
      var newArr = arr.concat();
      // basic sanity check
      if (newArr.length != arr.length) throw 666;
      return newArr;
    };
}


// ----------------------------------------------------------------------------
// Benchmark: ConcatArg
// ----------------------------------------------------------------------------


function ConcatArg(arr) {
  return () => {
      var newArr = [].concat(arr);
      // basic sanity check
      if (newArr.length != arr.length) throw 666;
      return newArr;
    };
}

// ----------------------------------------------------------------------------
// Benchmark: ForOfPush
// ----------------------------------------------------------------------------


function ForOfPush(arr) {
  return () => {
      var newArr = [];
      for (let x of arr) {
        newArr.push(x)
      }
      // basic sanity check
      if (newArr.length != arr.length) throw 666;
      return newArr;
    };
}

// ----------------------------------------------------------------------------
// Benchmark: MapId
// ----------------------------------------------------------------------------


function MapId(arr) {
  return () => {
      var newArr = arr.map(x => x);
      // basic sanity check
      if (newArr.length != arr.length) throw 666;
      return newArr;
    };
}

// ----------------------------------------------------------------------------
// Setup and Run
// ----------------------------------------------------------------------------

load('../base.js');

var success = true;

function PrintResult(name, result) {
  print(name + '-ArrayLiteralInitialSpread(Score): ' + result);
}

function PrintError(name, error) {
  PrintResult('Error: ' + name, error);
  success = false;
}

function CreateBenchmark(name, f) {
  new BenchmarkSuite(name, [1000], [ new Benchmark(name, false, false, 0, f) ]);
}

CreateBenchmark('Spread-Small', Spread(smallArray));
CreateBenchmark('ForLength-Small', ForLength(smallArray));
CreateBenchmark('ForLengthEmpty-Small', ForLengthEmpty(smallArray));
CreateBenchmark('Slice-Small', Slice(smallArray));
CreateBenchmark('Slice0-Small', Slice0(smallArray));
CreateBenchmark('ConcatReceive-Small', ConcatReceive(smallArray));
CreateBenchmark('ConcatArg-Small', ConcatArg(smallArray));
CreateBenchmark('ForOfPush-Small', ForOfPush(smallArray));
CreateBenchmark('MapId-Small', MapId(smallArray));


CreateBenchmark('Spread-Large', Spread(largeArray));
CreateBenchmark('ForLength-Large', ForLength(largeArray));
CreateBenchmark('ForLengthEmpty-Large', ForLengthEmpty(largeArray));
CreateBenchmark('Slice-Large', Slice(largeArray));
CreateBenchmark('Slice0-Large', Slice0(largeArray));
CreateBenchmark('ConcatReceive-Large', ConcatReceive(largeArray));
CreateBenchmark('ConcatArg-Large', ConcatArg(largeArray));
CreateBenchmark('ForOfPush-Large', ForOfPush(largeArray));
CreateBenchmark('MapId-Large', MapId(largeArray));


CreateBenchmark('Spread-SmallHoley', Spread(smallHoleyArray));
CreateBenchmark('ForLength-SmallHoley', ForLength(smallHoleyArray));
CreateBenchmark('ForLengthEmpty-SmallHoley', ForLengthEmpty(smallHoleyArray));
CreateBenchmark('Slice-SmallHoley', Slice(smallHoleyArray));
CreateBenchmark('Slice0-SmallHoley', Slice0(smallHoleyArray));
CreateBenchmark('ConcatReceive-SmallHoley', ConcatReceive(smallHoleyArray));
CreateBenchmark('ConcatArg-SmallHoley', ConcatArg(smallHoleyArray));
CreateBenchmark('ForOfPush-SmallHoley', ForOfPush(smallHoleyArray));
CreateBenchmark('MapId-SmallHoley', MapId(smallHoleyArray));


CreateBenchmark('Spread-LargeHoley', Spread(largeHoleyArray));
CreateBenchmark('ForLength-LargeHoley', ForLength(largeHoleyArray));
CreateBenchmark('ForLengthEmpty-LargeHoley', ForLengthEmpty(largeHoleyArray));
CreateBenchmark('Slice-LargeHoley', Slice(largeHoleyArray));
CreateBenchmark('Slice0-LargeHoley', Slice0(largeHoleyArray));
CreateBenchmark('ConcatReceive-LargeHoley', ConcatReceive(largeHoleyArray));
CreateBenchmark('ConcatArg-LargeHoley', ConcatArg(largeHoleyArray));
CreateBenchmark('ForOfPush-LargeHoley', ForOfPush(largeHoleyArray));
CreateBenchmark('MapId-LargeHoley', MapId(largeHoleyArray));


BenchmarkSuite.config.doWarmup = undefined;
BenchmarkSuite.config.doDeterministic = undefined;
BenchmarkSuite.RunSuites({NotifyResult: PrintResult, NotifyError: PrintError});
