// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-jspi --wasm-stack-switching-stack-size=100 --async-stack-traces

utils.load('test/inspector/wasm-inspector-test.js');

let {session, contextGroup, Protocol} = InspectorTest.start(
    'Test async stack traces with wasm jspi');

// test.js

function instantiateWasm(bytes) {
  var buffer = new ArrayBuffer(bytes.length);
  var view = new Uint8Array(buffer);
  for (var i = 0; i < bytes.length; ++i) {
    view[i] = bytes[i] | 0;
  }

  var module = new WebAssembly.Module(buffer);

  async function js_func(f) {
    return await f();
  };
  const wasmjs_func = new WebAssembly.Function({parameters:['externref', 'externref'], results:['i32']}, js_func, {suspending: 'first'});

  const instance = new WebAssembly.Instance(module, {env: {wrapped: wasmjs_func}});
  const wasmWrapperFunc = new WebAssembly.Function({parameters: ['externref'], results:['externref']}, instance.exports.threeTimes, {promising: 'first'});

  async function wrapperFunc(f) {
    // JS function that calls wasm should show up on the call stack.
    return await wasmWrapperFunc(f);
  }

  return wrapperFunc;
}

function doPause() {
  console.log(`Error location: ${new Error().stack}\n`);
  debugger;
}

async function testSimple() {
  doPause();
  return 1;
}

async function testSetTimeout() {
  const result = await new Promise(r => setTimeout(() => r(1), 0));
  doPause();
  return result;
}

async function testSyncThrow() {
  throw 'fail';
}

async function testAsyncThrow() {
  await Promise.resolve();
  throw 'fail';
}

async function testCatch(f) {
  try {
    await wrapperFunc(f);
  } catch (e) {
    console.log('caught: ' + e);
  }
}

async function testDontCatch(f) {
  let resolveFunc = null;
  const done = new Promise(res => {resolveFunc = res});
  wrapperFunc(f).finally(()=>resolveFunc());
  await done;
}

// Generate Wasm module

const kSig_i_rr = makeSig([kWasmExternRef, kWasmExternRef], [kWasmI32]);

var builder = new WasmModuleBuilder();

// All functions take a suspender, the js async function to call, and
// return int.

// Add two functions that will be suspended by calling an async
// JS function

const wrapped_js = builder.addImport('env', 'wrapped', kSig_i_rr);
const wrapped_wasm = builder.addFunction('wrappedWasm', kSig_i_rr, ['suspender', 'js_func'])
    .addBody([
      // Load a parameter and call the import.
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, wrapped_js,
    ]);

const main = builder.addFunction('threeTimes', kSig_i_rr)
    .addBody([
      // Call function 'wrappedWasm' three times.
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, 1,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, wrapped_wasm.index,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallFunction, 1,
      kExprI32Add,
      kExprI32Add,
    ])
    .exportFunc();

var module_bytes = builder.toArray();

// Create debuggee script

const helpers = [instantiateWasm, doPause];
const testPauseFunctions = [testSimple, testSetTimeout];
const testThrowFunctions = [testSyncThrow, testAsyncThrow];
const testCatchFunctions = [testCatch, testDontCatch];

const file = [...helpers, ...testPauseFunctions, ...testThrowFunctions, ...testCatchFunctions].join('\n\n') + `
const wrapperFunc = instantiateWasm(${JSON.stringify(module_bytes)});
`;
const startLine = 14;  // Should match first line of first function
contextGroup.addScript(file, startLine, 0, 'test.js');

// Initialize debugger

let predictedUncaught = null;
let actuallyUncaught = null;

session.setupScriptMap();
Protocol.Runtime.enable();
Protocol.Debugger.enable();
Protocol.Debugger.setAsyncCallStackDepth({maxDepth: 6});
Protocol.Debugger.setPauseOnExceptions({state: 'all'});
Protocol.Debugger.onPaused(message => {
  predictedUncaught = message.params.data?.uncaught;
  InspectorTest.log('Debugger paused on ' + (((predictedUncaught && 'uncaught exception') ?? 'debugger statement') || 'caught exception'));
  session.logCallFrames(message.params.callFrames);
  session.logAsyncStackTrace(message.params.asyncStackTrace);
  InspectorTest.log('');
  Protocol.Debugger.resume();
});

Protocol.Console.enable();
Protocol.Console.onMessageAdded(event => {
  InspectorTest.log('console: ' + event.params.message.text);
});

Protocol.Runtime.onExceptionRevoked(event => {
  actuallyUncaught = false;
});
Protocol.Runtime.onExceptionThrown(event => {
  actuallyUncaught = true;
});
Protocol.Runtime.enable();

// Run tests

InspectorTest.runAsyncTestSuite([
  // async function testAsyncStackTracesOnPauseAndError() {
  //   for (const testFunc of testPauseFunctions) {
  //     InspectorTest.log(`Testing async callstacks in JSPI with test function ${testFunc.name}`);
  //     const {result} = await Protocol.Runtime.evaluate({expression: `wrapperFunc(${testFunc.name})//# sourceURL=test_framework.js`, awaitPromise: true});

  //     InspectorTest.log(`Returned result ${JSON.stringify(result)}`);
  //     InspectorTest.log('');
  //   }
  // },
  async function testCatchPrediction() {
    for (const testCatchFunc of testCatchFunctions) {
      for (const testThrowFunc of testThrowFunctions) {
        InspectorTest.log(`Testing catch prediction through JSPI throwing from ${testThrowFunc.name} to ${testCatchFunc.name}`);
        actuallyUncaught = false;
        predictedUncaught = null;
        const {result} = await Protocol.Runtime.evaluate({expression: `${testCatchFunc.name}(${testThrowFunc.name})//# sourceURL=test_framework.js`, awaitPromise: true});
        InspectorTest.log(`Returned result ${JSON.stringify(result)}`);
        if (actuallyUncaught) {
          InspectorTest.log('Exception was not caught');
        }
        if (actuallyUncaught !== predictedUncaught) {
          InspectorTest.log(`PREDICTION MISMATCH: predicted uncaught=${predictedUncaught} actual uncaught=${actuallyUncaught}`);
        }
        InspectorTest.log('');
      }
    }
  }
]);
