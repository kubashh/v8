// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

Debug = debug.Debug;

var array = [1,2,3];

function update_second() { array[1] = 0; }

function success(expectation, source) {
  assertEquals(expectation,
               Debug.evaluateGlobal(source, true).value());
}
function fail(source) {
  assertThrows(() => Debug.evaluateGlobal(source, true),
               EvalError);
}

// checks that we patch function code with runtime check on each side effect free evaluate
fail('update_second()');
fail('update_second()');
