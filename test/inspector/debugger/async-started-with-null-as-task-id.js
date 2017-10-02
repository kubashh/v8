// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} =
  InspectorTest.start('Checks no break on asyncTaskStarted(0)');

(async function test() {
  Protocol.Debugger.enable();
  Protocol.Debugger.setAsyncCallStackDepth({maxDepth: 128});
  InspectorTest.log('Call asyncTaskStarted(0) and check no break after..');
  let evaluation = Protocol.Runtime.evaluate({
    expression: 'inspector.asyncTaskStarted(0); (() => 0)()'
  });
  let paused = Protocol.Debugger.oncePaused();
  InspectorTest.log('Should be evaluation result (not a paused event):');
  InspectorTest.logMessage(await Promise.race([evaluation, paused]));
  InspectorTest.completeTest();
})();
