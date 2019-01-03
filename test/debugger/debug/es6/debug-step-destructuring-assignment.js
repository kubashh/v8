// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


var exception = null;
var Debug = debug.Debug;
var break_count = 0;

function listener(event, exec_state, event_data, data) {
  if (event != Debug.DebugEvent.Break) return;
  try {
    var source = exec_state.frame(0).sourceLineText();
    print(source);
    assertTrue(source.indexOf(`// B${break_count++}`) > 0);
    if (source.indexOf("assertEquals") > 0) {
      exec_state.prepareStep(Debug.StepAction.StepNext);
    } else {
      exec_state.prepareStep(Debug.StepAction.StepIn);
    }
  } catch (e) {
    exception = e;
    print(e);
  }
};

Debug.setListener(listener);

function f() {
  var a, b, c, d;
  debugger;                                       // B0
  [
    a,                                            // B1
    b,                                            // B2
    c = 3                                         // B3
  ] = [1, 2];
  assertEquals({a:1,b:2,c:3}, {a, b, c});         // B4

  [
    a,                                            // B5
    [
      b,                                          // B6
      c                                           // B7
    ],
    d                                             // B8
  ] = [5, [6, 7], 8];
  assertEquals({a:5,b:6,c:7,d:8}, {a, b, c, d});  // B9

  [
    a,                                            // B10
    b,                                            // B11
    ...c                                          // B12
  ] = [1, 2, 3, 4];
  assertEquals({a:1,b:2,c:[3,4]}, {a, b, c});     // B13

  ({
    a,                                            // B14
    b,                                            // B15
    c = 7                                         // B16
  } = {a: 5, b: 6});
  assertEquals({a:5,b:6,c:7}, {a, b, c});         // B17

  ({
    a,                                            // B18
    b = return1(),                                // B19
    c = return1()                                 // B20
  } = {a: 5, b: 6});
  assertEquals({a:5,b:6,c:1}, {a, b, c});         // B23

  ({
    x : a,                                        // B24
    y : b,                                        // B25
    z : c = 3                                     // B26
  } = {x: 1, y: 2});
  assertEquals({a:1,b:2,c:3}, {a, b, c});         // B27
}                                                 // B28

function return1() {
  return 1                                        // B21
  ;                                               // B22
}

f();
Debug.setListener(null);                          // B29
assertNull(exception);
