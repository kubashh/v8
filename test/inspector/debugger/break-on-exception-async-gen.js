// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Check that "break on uncaught" exceptions works for async generators.');

contextGroup.addScript(`
async function* simpleThrow() {
  throw new Error();
}

async function* yieldBeforeThrow() {
  yield 1;
  throw new Error();
}

async function* awaitBeforeThrow() {
  await 1;
  throw new Error();
}

async function* yieldBeforeThrowWithAwait() {
  await 1;
  yield 2;
  throw new Error();
}

async function* awaitBeforeThrowWithYield() {
  yield 1;
  await 2;
  throw new Error();
}

async function* yieldThrows() {
  yield 1;
  yield thrower();
}

async function* awaitThrows() {
  yield 1;
  await thrower();
}

async function runGen(gen) {
  try {
    for await (const val of gen());
  } catch {}
}

async function thrower() {
  await 1;  // Suspend once.
  throw new Error();
}`);

async function runTest(expression) {
  await Promise.all([
    Protocol.Debugger.enable(),
    Protocol.Debugger.setPauseOnExceptions({state: 'uncaught'}),
  ]);

  // Don't pause.
  await Protocol.Runtime.evaluate({ expression, awaitPromise: true });

  // Run the same expression but with 'caught' and expect a pause.
  await Protocol.Debugger.setPauseOnExceptions({ state: 'caught' });
  const evalPromise = Protocol.Runtime.evaluate({ expression, awaitPromise: true });
  await Protocol.Debugger.oncePaused();
  InspectorTest.log('SUCCESS');

  await Promise.all([
    Protocol.Debugger.disable(),
    evalPromise,
  ]);
}

InspectorTest.runAsyncTestSuite([
  async function testSimpleGeneratorThrow() {
    await runTest('runGen(simpleThrow)');
  },
  async function testYieldBeforeThrow() {
    await runTest('runGen(yieldBeforeThrow)');
  },
  async function testAwaitBeforeThrow() {
    await runTest('runGen(awaitBeforeThrow)');
  },
  async function testYieldBeforeThrowWithAwait() {
    await runTest('runGen(yieldBeforeThrowWithAwait)');
  },
  async function testAwaitBeforeThrowWithYield() {
    await runTest('runGen(awaitBeforeThrowWithYield)');
  },
  async function testYieldThrows() {
    await runTest('runGen(yieldThrows)');
  },
  async function testAwaitThrows() {
    await runTest('runGen(awaitThrows)');
  },
]);
