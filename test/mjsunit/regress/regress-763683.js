// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var __v_0 = [];
;
var __v_2 = {};
var __v_3 = {};
var __v_4 = {};
var __v_5 = {};
var __v_6 = {};
var __v_7 = 0;
var __v_8 = {};
var __v_9 = {};
var __v_10 = {};
var __v_11 = {};
var __v_12 = {};
var __v_13 = (new Boolean(false));
var __v_14 = -1.79769313486e+308;

function __f_0() {
  for (var idx = 0; idx < __v_3.length; idx++) {
    try {
      var foo = __v_5[__v_7];
    } catch(e) {"Caught: " + e; }
  }
}


// Tail call proxy function when caller has an arguments adaptor frame.
try { (function test() {
  // Caller and callee have same number of arguments.
  function __f_1(__v_0) {
  }
  try { 1, __v_5 = new Proxy(__f_1, {}); } catch(e) { print("Caught: " + e); }

  // Caller has more arguments than callee.
  function __f_2(__v_0) {
    try { __v_3 = __v_6, gc(); } catch(e) { print("Caught: " + e); }
    try {
      __f_0();
    } catch(e) { print("Caught: " + e); }
  }
  try { 1, __v_6 = new Proxy(__f_2, {}); } catch(e) { print("Caught: " + e); }
  function __f_6(__v_0, __v_1, __v_2) { try { return __v_6(2); } catch(e) { print("Caught: " + e); } }
  try { assertEquals(12, __f_6()); } catch(e) {"Caught: " + e; }

  // Caller has less arguments than callee.
  function __f_3(__v_0, __v_2) {
  }
  try { 1, __v_7 = new Proxy(__f_3, {}); } catch(e) { print("Caught: " + e); }

  // Callee has arguments adaptor frame.
  function __f_4(__v_0, __v_1, __v_2) {
    try {
      __f_0();
    } catch(e) {
      print("Caught: " + e);
    }
  }
  try { 1, __v_8 = new Proxy(__f_4, {}); } catch(e) { print(); }
  function __f_8(__v_0) {
    try {
      return __v_8(2);
    } catch(e) {
      print("Caught: " + e);
    }
  }
  try { assertEquals(12, __f_8()); } catch(e) {"Caught: " + e; }
})(); } catch(e) { print("Caught: " + e); }
