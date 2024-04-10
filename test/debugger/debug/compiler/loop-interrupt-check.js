// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turboshaft-from-maglev --turbofan

// This test checks that loop interrupt stack checks are properly detected are
// processed.

var Debug = debug.Debug;

var trigger_count = 0;
var called_from;
Debug.setListener(function (event, exec_state, event_data, data) {
  if (event == Debug.DebugEvent.Break) {
    called_from = exec_state.frames[0].functionName;
    trigger_count += 1;
  }
});

function g(x) {
  if (x == 0) {
    %ScheduleBreak();
  }
  return 7;
}

function loop_interrupt_check_f(x) {
  let r = 0;
  for (let i = 0; i < 10; i++) {
    if (i == 0) {
      // Schedules an interrupt if {x} is 0.
      r += g(x);
    } else {
      r += 7;
    }
  }
  return r;
}

%PrepareFunctionForOptimization(loop_interrupt_check_f);
assertEquals(70, loop_interrupt_check_f(1));
%OptimizeFunctionOnNextCall(loop_interrupt_check_f);
assertEquals(70, loop_interrupt_check_f(1));
assertOptimized(loop_interrupt_check_f);

assertEquals(0, trigger_count);
assertEquals(70, loop_interrupt_check_f(0));
assertEquals(1, trigger_count);
assertEquals(called_from, "loop_interrupt_check_f");
assertOptimized(loop_interrupt_check_f);
