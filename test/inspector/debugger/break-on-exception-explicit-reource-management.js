// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start(
    'Checks if catch prediction works on new `using` and `await using` syntax.');

Protocol.Debugger.enable();
Protocol.Debugger.onPaused(({params: {data}}) => {
  InspectorTest.log('paused on exception:');
  InspectorTest.logMessage(data);
  Protocol.Debugger.resume();
});

contextGroup.addInlineScript(
    `
function disposalThrowsInUsingSyntax() {
    using x = {
      value: 1,
      [Symbol.dispose]() {
    throw new Error("fail");
    }
    };
}

async function disposalThrowsInAwaitUsingSyntax() {
    using y = {
      value: 2,
      [Symbol.asyncDispose]() {
    throw new Error("fail");
    }
    };
}
`,
    'test.js');

InspectorTest.runAsyncTestSuite([
  async function testPauseOnInitialState() {
    await evaluate('disposalThrowsInUsingSyntax()');
    await evaluate('disposalThrowsInAwaitUsingSyntax()');
  },

  async function testPauseOnExceptionOff() {
    await Protocol.Debugger.setPauseOnExceptions({state: 'none'});
    await evaluate('disposalThrowsInUsingSyntax()');
    await evaluate('disposalThrowsInAwaitUsingSyntax()');
    await Protocol.Debugger.setPauseOnExceptions({state: 'none'});
  },

  async function testBreakOnCaughtException() {
    await Protocol.Debugger.setPauseOnExceptions({state: 'caught'});
    await evaluate('disposalThrowsInUsingSyntax()');
    await evaluate('disposalThrowsInAwaitUsingSyntax()');
    await Protocol.Debugger.setPauseOnExceptions({state: 'none'});
  },

  async function testBreakOnUncaughtException() {
    await Protocol.Debugger.setPauseOnExceptions({state: 'uncaught'});
    await evaluate('disposalThrowsInUsingSyntax()');
    await evaluate('disposalThrowsInAwaitUsingSyntax()');
    await Protocol.Debugger.setPauseOnExceptions({state: 'none'});
  },

  async function testBreakOnAll() {
    await Protocol.Debugger.setPauseOnExceptions({state: 'all'});
    await evaluate('disposalThrowsInUsingSyntax()');
    await evaluate('disposalThrowsInAwaitUsingSyntax()');
    await Protocol.Debugger.setPauseOnExceptions({state: 'none'});
  },

  async function testBreakOnExceptionInSilentMode(next) {
    await Protocol.Debugger.setPauseOnExceptions({state: 'all'});
    InspectorTest.log(`evaluate 'disposalThrowsInUsingSyntax()'`);
    await Protocol.Runtime.evaluate(
        {expression: 'disposalThrowsInUsingSyntax()', silent: true});
    InspectorTest.log(`evaluate 'disposalThrowsInAwaitUsingSyntax()'`);
    await Protocol.Runtime.evaluate(
        {expression: 'disposalThrowsInAwaitUsingSyntax()', silent: true});
    await Protocol.Debugger.setPauseOnExceptions({state: 'none'});
  }
]);

async function evaluate(expression) {
  InspectorTest.log(`\nevaluate '${expression}'..`);
  contextGroup.addInlineScript(expression);
  await InspectorTest.waitForPendingTasks();
}
