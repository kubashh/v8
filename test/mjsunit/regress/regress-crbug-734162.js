// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


(function TestSmi() {
  var __v_0 = {};
  function __f_0(constructor, closure) {
    var __v_2 = { value: 0 };
    __v_4 = closure(constructor, 1073741823, __v_0, __v_2);
    assertEquals(1, __v_2.value);
  }
  function __f_1(constructor, val, deopt, __v_2) {
    if (!new constructor(val, deopt, __v_2)) {
    }
  }
  function __f_10(constructor) {
    __f_0(constructor, __f_1);
    __f_0(constructor, __f_1);
    __f_0(constructor, __f_1);
  }
  function __f_12(val, deopt, __v_2) {
    __v_2.value++;
  }
  __f_10(__f_12);
})();

(function TestHeapNumber() {
  var __v_0 = {};
  function __f_0(constructor, closure) {
    var __v_2 = { value: 1.5 };
    __v_4 = closure(constructor, 1073741823, __v_0, __v_2);
    assertEquals(2.5, __v_2.value);
  }
  function __f_1(constructor, val, deopt, __v_2) {
    if (!new constructor(val, deopt, __v_2)) {
    }
  }
  function __f_10(constructor) {
    __f_0(constructor, __f_1);
    __f_0(constructor, __f_1);
    __f_0(constructor, __f_1);
  }
  function __f_12(val, deopt, __v_2) {
    __v_2.value++;
  }
  __f_10(__f_12);
})();
