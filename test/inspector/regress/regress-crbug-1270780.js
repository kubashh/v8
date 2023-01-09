// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Regression test for crbug.com/1270780.');

contextGroup.addScript(`
function* gen() {
  yield 1;
  throw new Error();
}

function callGen() {
  try {
    for (const x of gen()) {}
  } catch {}
}

const throwNextIter = {
  values: [1, 2],
  next() {
    // Throw an exception once 'values' is empty.
    if (this.values.length === 0) {
      throw new Error();
    }
    return {value: this.values.shift(), done: false};
  },
  [Symbol.iterator]() { return this; }
};

const throwReturnIter = {
  values: [1, 2],
  next() {
    if (this.values.length === 0) { return {done: true}};
    return {value: this.values.shift(), done: false};
  },
  return() {
    throw new Error();
  },
  [Symbol.iterator]() { return this; }
};

function iterateIterator(iter) {
  try {
    for (const x of iter) {}
  } catch {}
}

function iterWithBreak(iter) {
  try {
    for (const x of iter) {
      break;  // Triger '.return'.
    }
  } catch {}
}`);

async function runTest(expression) {
  await Promise.all([
    Protocol.Debugger.enable(),
    Protocol.Debugger.setPauseOnExceptions({state: 'uncaught'}),
  ]);

  // Don't pause.
  await Protocol.Runtime.evaluate({ expression, replMode: true });

  // Run the same expression but with 'caught' and expect a pause.
  await Protocol.Debugger.setPauseOnExceptions({ state: 'caught' });
  const evalPromise = Protocol.Runtime.evaluate({ expression, replMode: true });
  await Protocol.Debugger.oncePaused();
  InspectorTest.log('SUCCESS');

  await Promise.all([
    Protocol.Debugger.disable(),
    evalPromise,
  ]);
}

InspectorTest.runAsyncTestSuite([
  async function testThrowingGenerator() {
    await runTest('callGen()');
  },
  async function testThrowingNext() {
    await runTest('iterateIterator(throwNextIter)');
  },
  async function testThrowingReturn() {
    await runTest('iterWithBreak(throwReturnIter)');
  },
]);
