// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Tests that evaluation works when code generation from strings is not allowed.');

(async function test() {
  Protocol.Debugger.enable();
  Protocol.Runtime.enable();
  contextGroup.setAllowCodeGenerationFromStrings(false);
  contextGroup.addScript(
  `var global = 'Global';
  function foo(x) {
    var local = 'Local';
    debugger;
    return local + x;
  }
  foo();`);
  await Protocol.Debugger.onceScriptParsed();
  let {params:{callFrames}} = await Protocol.Debugger.oncePaused();
  var callFrameId = callFrames[0].callFrameId;

  await Protocol.Runtime.evaluate({ expression: "global" })
      .then(InspectorTest.logMessage);
  await Protocol.Debugger.evaluateOnCallFrame({ callFrameId, expression: "local" })
      .then(InspectorTest.logMessage);
  InspectorTest.completeTest();
})();
