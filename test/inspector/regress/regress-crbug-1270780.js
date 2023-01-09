// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Regression test for crbug.com/1270780.');

const script = `
function* gen() {
  yield 1;
  throw new Error();
}

try {
  for (const x of gen()) {}
} catch {}`;

InspectorTest.runAsyncTestSuite([
  async function testDontBreakOnCaughtException() {
    let didPause = false;
    Protocol.Debugger.onPaused(() => { didPause = true; });

    await Promise.all([
      Protocol.Debugger.enable(),
      Protocol.Debugger.setPauseOnExceptions({state: 'uncaught'}),
    ]);

    // Don't pause.
    contextGroup.addScript(script);

    await Protocol.Debugger.disable(),

    InspectorTest.log(didPause ? 'UNEXPECTED PAUSE' : 'SUCCESS');
  },
]);
