// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

Debug = debug.Debug;

var exception = null;

function listener(event, exec_state, event_data, data) {
  try {
    if (event == Debug.DebugEvent.Break) {
      assertEquals(exec_state.frame(0).evaluate("o.#foo").value(), 42);
    }
  } catch (e) {
    exception = e;
  }
}

Debug.setListener(listener);

(function testPrivateName() {
  class C {
    m(o) { debugger; }
    #foo = 42
  }
  const c = new C;
  c.m(c);
})();
assertNull(exception);

(function testHeritageExpressionScope() {
  let exfil;
  class C extends (function () {
    exfil = (o) => { debugger; }
  }) {
    #foo = 0;
  }
  const c = new C;
  exfil(c);
})();
assertInstanceof(exception, SyntaxError);

Debug.setListener(null);
