// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// In the interpreter, this leads to a crash. The interpreter will be defunc'ed
// soon anyway, so just ignore that crash for now, and always use Liftoff.
// Flags: --debug-in-liftoff

const {session, contextGroup, Protocol} =
    InspectorTest.start('Test scope inspection and stepping after a trap.');
session.setupScriptMap();

utils.load('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

// Create a function which recursively calls itself.
builder.addFunction('div', kSig_i_iii)
    .addLocals({}, ['a', 'b', 'unused'])
    .addBody([
      kExprLocalGet, 0,  // param 0
      kExprLocalGet, 1,  // param 1
      kExprI32DivS       // div
    ])
    .exportFunc();

const module_bytes = JSON.stringify(builder.toArray());

function instantiate(bytes) {
  let buffer = new ArrayBuffer(bytes.length);
  let view = new Uint8Array(buffer);
  for (let i = 0; i < bytes.length; ++i) {
    view[i] = bytes[i] | 0;
  }

  let module = new WebAssembly.Module(buffer);
  return new WebAssembly.Instance(module);
}

function getShortLocationString(location) {
  return `${location.lineNumber}:${location.columnNumber}`;
}

let actions = ['stepInto', 'resume', 'stepInto', 'resume'];
Protocol.Debugger.onPaused(async msg => {
  InspectorTest.log('Paused at:');
  for (let [nr, frame] of msg.params.callFrames.entries()) {
    InspectorTest.log(`--- ${nr} ---`);
    await session.logSourceLocation(frame.location);
    // TODO(clemensb): Also print local scope (after fixing).
  }
  InspectorTest.log('-------------');
  let action = actions.shift();
  InspectorTest.log(`-> ${action}`);
  Protocol.Debugger[action]();
});

function call_div() {
  instance.exports.div(0, 1, 4711);  // does not trap
  try {
    instance.exports.div(1, 0, 4711);  // traps (div by zero)
  } catch (e) {
    return e;
  }
  try {
    instance.exports.div(0x7fffffff, -1, 4711);  // traps (unrepresentable)
  } catch (e) {
    return e;
  }
}

contextGroup.addScript(instantiate.toString());
contextGroup.addScript(call_div.toString());

(async function test() {
  await Protocol.Debugger.enable();
  await Protocol.Debugger.setPauseOnExceptions({state: 'all'});
  InspectorTest.log('Instantiating.');
  await Protocol.Runtime.evaluate(
      {'expression': `const instance = instantiate(${module_bytes});`});
  InspectorTest.log('Calling div function.');
  await Protocol.Runtime.evaluate({'expression': 'call_div()'});
  InspectorTest.log('Finished.');
  InspectorTest.completeTest();
})();
