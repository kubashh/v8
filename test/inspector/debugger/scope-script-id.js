// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} =
    InspectorTest.start('Checks scriptId of scope locations.');

contextGroup.addScript(`
function f1() {
  eval('function f2() {' +
    'eval("function f3() {debugger} f3() //# sourceURL=f3.js");}' +
    'f2() //# sourceURL=f2.js');
}
//# sourceURL=f1.js`);

(async function test() {
  session.setupScriptMap();
  Protocol.Debugger.enable();
  Protocol.Runtime.evaluate({expression: 'f1()'})
      .then(msg => InspectorTest.logMessage(msg));
  const {params: {callFrames: [{scopeChain}]}} =
      await Protocol.Debugger.oncePaused();
  for (const scope of scopeChain) {
    // TODO(kozyatinskiy): each closure scope should have proper location.
    const {startLocation, endLocation} = scope;
    InspectorTest.logMessage(scope);
    if (startLocation && endLocation) {
      InspectorTest.log(`scope location:`);
      await session.logSourceLocation(startLocation, false, true);
      await session.logSourceLocation(endLocation, false, true);
    }
  }
  InspectorTest.completeTest();
})()
