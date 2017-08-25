// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} =
  InspectorTest.start(`Tests Debugger.continueToLocation with targetCallFrames
is deeper.`);

contextGroup.addScript(`
function test() {
  callCallback(foo);
}

function callCallback(callback) {
  callback();
}

function foo() {
}

function testAsync() {
  callCallbackAsync(foo);
  foo();
}

function callCallbackAsync(callback) {
  setTimeout(callback, 0);
}
//# sourceURL=test.js`)

session.setupScriptMap();
InspectorTest.runAsyncTestSuite([
  async function testSyncStack() {
    Protocol.Debugger.enable();
    let {params:{scriptId}} = await Protocol.Debugger.onceScriptParsed();
    Protocol.Debugger.pause();
    Protocol.Runtime.evaluate({expression: 'test()'});
    let {params:{callFrames}} = await Protocol.Debugger.oncePaused();
    await session.logSourceLocation(callFrames[0].location);
    Protocol.Debugger.continueToLocation({location: {
      scriptId,
      lineNumber: 2,
      columnNumber: 2
    }});
    ({params:{callFrames}} = await Protocol.Debugger.oncePaused());
    await session.logSourceLocation(callFrames[0].location);
    Protocol.Debugger.continueToLocation({location: {
      scriptId,
      lineNumber: 10,
      columnNumber: 0
    }, targetCallFrames: 'deeper'});
    ({params:{callFrames}} = await Protocol.Debugger.oncePaused());
    await session.logSourceLocation(callFrames[0].location);
    Protocol.Debugger.resume();
    await Protocol.Debugger.disable();
  },

  async function testAsyncStack() {
    Protocol.Debugger.enable();
    Protocol.Debugger.setAsyncCallStackDepth({maxDepth: 128});
    let {params:{scriptId}} = await Protocol.Debugger.onceScriptParsed();

    Protocol.Debugger.pause();
    Protocol.Runtime.evaluate({expression: 'testAsync()//# sourceURL=expr.js'});
    let {params:{callFrames, asyncStackTrace}} = await Protocol.Debugger.oncePaused();

    await session.logSourceLocation(callFrames[0].location);
    session.logCallFrames(callFrames);
    session.logAsyncStackTrace(asyncStackTrace);
    Protocol.Debugger.continueToLocation({location: {
      scriptId,
      lineNumber: 13,
      columnNumber: 2
    }});
    ({params:{callFrames, asyncStackTrace}} = await Protocol.Debugger.oncePaused());
    session.logCallFrames(callFrames);
    session.logAsyncStackTrace(asyncStackTrace);
    InspectorTest.log('');
    await session.logSourceLocation(callFrames[0].location);

    Protocol.Debugger.continueToLocation({location: {
      scriptId,
      lineNumber: 10,
      columnNumber: 0
    }, targetCallFrames: 'deeper'});
    ({params:{callFrames, asyncStackTrace}} = await Protocol.Debugger.oncePaused());
    session.logCallFrames(callFrames);
    session.logAsyncStackTrace(asyncStackTrace);
    InspectorTest.log('');
    await session.logSourceLocation(callFrames[0].location);
    await Protocol.Debugger.disable();
  }
]);
