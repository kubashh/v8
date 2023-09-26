// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// We use "r" for nullable "externref", and "e" for non-nullable "ref extern".
let kSig_i_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI32]);

let kDataViewGetInt32C;
let kDataViewGetInt16C;

function MakeBuilder() {
  let builder = new WasmModuleBuilder();

  kDataViewGetInt32C = builder.addImport(
    'DataView', 'getInt32A', kSig_i_rii);
  kDataViewGetInt16C = builder.addImport(
      'DataView', 'getInt16A', kSig_i_rii);

  return builder;
}

let kImports = {
  DataView: {
    getInt32A:Function.prototype.call.bind(DataView.prototype.getInt32),
    getInt16A:Function.prototype.call.bind(DataView.prototype.getInt16)
  },
};

function CheckStackTrace(thrower, reference, topmost_wasm_func) {
  let reference_exception;
  let actual_exception;
  try {
    thrower();
    assertUnreachable();
  } catch (e) {
    actual_exception = e;
  }
  try {
    reference();
    assertUnreachable();
  } catch (e) {
    reference_exception = e;
  }
  assertInstanceof(actual_exception, reference_exception.constructor);
  let actual_stack = actual_exception.stack.split('\n');
  let reference_stack = reference_exception.stack.split('\n');
  assertEquals(reference_stack[0], actual_stack[0]);
  assertEquals(reference_stack[1], actual_stack[1]);
  console.log(actual_stack[2]);
  assertTrue(
      actual_stack[2].startsWith(`    at ${topmost_wasm_func} (wasm://wasm/`));
}

(function TestGetInt32() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction("getInt32B", kSig_i_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewGetInt32C,
    ]);
    builder.addFunction("getInt16B", kSig_i_rii).exportFunc().addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallFunction, kDataViewGetInt16C,
      ]);
  let instance = builder.instantiate(kImports);
  let array = new Int32Array(2);
  array[0] = 42;
  array[1]  = 0x12345678;

  let dataview = new DataView(array.buffer);

  // assertEquals(42, instance.exports.getInt32B(dataview, 0, 1));
  // // Littleendian test
  // assertEquals(0x12345678, instance.exports.getInt32B(dataview, 4, 1));
  // assertEquals(0x78563412, instance.exports.getInt32B(dataview, 4, 0));
  // // TODO: check error type.
  // // Offset negative
  // assertThrows(()=>{instance.exports.getInt32B(dataview, -1, 1)}/*, RangeError*/);
  // // Bounds check.
  // assertThrows(()=>{instance.exports.getInt32B(dataview, 8, 1)}/*, RangeError*/);
  // assertThrows(()=>{instance.exports.getInt32B(dataview, 0x7FFF_FFFF, 1)}/*, RangeError*/);

  CheckStackTrace(
    () => instance.exports.getInt32B("acs", 0, 1), () => DataView.prototype.getInt32.call("acs", 0, 1),
    'getInt32B');

    // assertThrows(()=>{DataView.prototype.getInt32.call("acs", 0, 1)}/*, RangeError*/);

  // // Detached buffer.
  // %ArrayBufferDetach(array.buffer);
  // assertThrows(()=>{instance.exports.getInt32B(dataview, 0, 1)}/*, TypeError*/);

  // Out of bounds offset.
  //instance.exports.getInt32B(dataview, -4, 1)

  // Typecheck.
  //instance.exports.getInt32B([0,1,2], 0, 1);

  // Detached check.
  //  %ArrayBufferDetach(array.buffer);
  //  instance.exports.getInt32B(dataview, 0, 1);

  // Out of bounds viewsize.
  //instance.exports.getInt32B(dataview, 8, 1);
  //instance.exports.getInt32B(dataview, 0x7FFF_FFFF, 1);
})();

// TODO: Add test whether the typecheck fails, see e.g. CheckStackTrace in recognize-imports.js (line 55)
