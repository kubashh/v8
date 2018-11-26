// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const __v_1458 = [function () {
}, ReferenceError, function (__v_1481) {
  return "(class c { get [" + __v_1481 + "]() {} });";
}];
let __v_1459 = [, 'c--'];
let __v_1460 = [function () {
}];
let __v_1461 = [function () {
}, function (__v_1500) {
  return "(function() { " + __v_1500 + " })()";
}];
function __f_430(__v_1503) {
  try {
      eval(__v_1503)
  } catch (__v_1505) {
  }
}
for (var __v_1462 = 0; __v_1462 < __v_1458.length; __v_1462 += 2) {
  for (var __v_1463 = 0; __v_1463 < __v_1459.length; ++__v_1463) {
      for (var __v_1465 = 0; __v_1465 < __v_1461.length; ++__v_1465) {
          __f_430(__v_1458[__v_1462](__v_1461[__v_1465](
          __v_1458[__v_1463])), __v_1458[__v_1462 + 1]);
      }
  }
}
