// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function runTest(test, testName) {
  let start = performance.now();
  test();
  let end = performance.now();
  print(testName + ": " + (end-start) + " ms");
}

let fast_iterations = 200000;
let dictionary_iterations = 4000;
let argument_iterations = 20000;

a = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14];
runTest(() => {
  for (let i = 0; i < fast_iterations; ++i) {
    a.slice();
  }
}, "smi copy");

a = [0, 1, 2, 3,, 5,, 7, 8,, 10, 11,, 13, 14];
runTest(() => {
  for (let i = 0; i < fast_iterations; ++i) {
    a.slice(i % a.length);
  }
}, "smi slice");

a = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, {}, 14];
runTest(() => {
  for (let i = 0; i < fast_iterations; ++i) {
    a.slice();
  }
}, "object copy");

a = [0, 1, 2, 3,, 5,, 7, 8,, 10, 11,, 13, {}];
runTest(() => {
  for (let i = 0; i < fast_iterations; ++i) {
    a.slice(i % a.length);
  }
}, "object slice");

let c = [0, 1, 2, 3, 5, 7, 8,, 10, 11, 13, 14];
c[150000] = 1;
c[150001] = 2;
runTest(() => {
  for (let i = 0; i < dictionary_iterations; ++i) {
    c.slice(i % a.length);
  }
}, "dictionary slice");

runTest(() => {
  for (let i = 0; i < argument_iterations; ++i) {
    (function(a,b) {
      Array.prototype.slice.call(arguments, i % arguments.length);
    })(1,2,3,4,5,6,7,10,12);
  }
}, "fast sloppy arguments slice");

runTest(() => {
    (function(a,b) {
      delete arguments[0];
      for (let i = 0; i < argument_iterations; ++i) {
        Array.prototype.slice.call(arguments, i % arguments.length);
      }
    })(1,2,3,4,5,6,7,10,12);
}, "slow sloppy arguments slice");
