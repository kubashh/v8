// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var Debug = debug.Debug;

var frame;

Debug.setListener(function (event, exec_state, event_data, data) {
  if (event == Debug.DebugEvent.Break) {
    frame = exec_state.frame(0);
  }
});

function makeCounter() {
  var result = 0;
  return () => ++result;
}

// Break on entry to a function which includes heap-allocated variables.
%ScheduleBreak();
makeCounter();

assertEquals(1, frame.localCount());
assertEquals('result', frame.localName(0));
assertEquals(undefined, frame.localValue(0).value());

assertEquals(3, frame.scopeCount());
assertEquals(debug.ScopeType.Local, frame.scope(0).scopeType());
assertEquals(debug.ScopeType.Script, frame.scope(1).scopeType());
assertEquals(debug.ScopeType.Global, frame.scope(2).scopeType());
