// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const start = performance.now();
function time(name) {
  const ms_since_start = (performance.now() - start).toFixed(1).padStart(7);
  print(`[${ms_since_start}] ${name}`);
}

let kRefExtern = wasmRefType(kWasmExternRef);

// We use "r" for nullable "externref", and "e" for non-nullable "ref extern".

let kSig_e_ii = makeSig([kWasmI32, kWasmI32], [kRefExtern]);
let kSig_e_v = makeSig([], [kRefExtern]);
let kSig_i_ri = makeSig([kWasmExternRef, kWasmI32], [kWasmI32]);
let kSig_i_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI32]);
let kSig_i_rr = makeSig([kWasmExternRef, kWasmExternRef], [kWasmI32]);
let kSig_i_riii = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32],
                          [kWasmI32]);
let kSig_ii_riii = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32],
                           [kWasmI32, kWasmI32]);
let kSig_e_i = makeSig([kWasmI32], [kRefExtern]);
let kSig_e_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32],
                         [kRefExtern]);
let kSig_e_rr = makeSig([kWasmExternRef, kWasmExternRef], [kRefExtern]);
let kSig_e_r = makeSig([kWasmExternRef], [kRefExtern]);

let kDataViewGetInt32C;

function MakeBuilder() {
  let builder = new WasmModuleBuilder();

  kDataViewGetInt32C = builder.addImport(
      'DataView', 'getInt32A', kSig_i_rii);

  return builder;
}

let kImports = {
  DataView: {getInt32A:Function.prototype.call.bind(DataView.prototype.getInt32)},
};

// Locals vs params, first comes the params
(function TestGetInt32() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction("getInt32B", kSig_i_rii).exportFunc()
    .addLocals(kWasmI32, 2)   // counter
    .addBody([
      // counter = 0;
      kExprI32Const, 0, kExprLocalSet, 3,
      // for_block
      kExprBlock, kWasmVoid,
        kExprLoop, kWasmVoid,
          // if counter == 8 jump after end_block;
          kExprLocalGet, 3, kExprI32Const, ...wasmSignedLeb(5000), kExprI32Eq, kExprBrIf, 1,
          // counter++;
          kExprLocalGet, 3, kExprI32Const, 1, kExprI32Add,
          kExprLocalSet, 3,
          // Set up and call `GetInt32`.
          kExprLocalGet, 0,
          kExprLocalGet, 1,
          kExprLocalGet, 2,
          kExprCallFunction, kDataViewGetInt32C,
          kExprLocalSet, 4,
          kExprBr, 0,
        kExprEnd,
      kExprEnd,
      // for_block
      kExprLocalGet, 4,
    ]);
  let instance = builder.instantiate(kImports);
  let array = new Int32Array(2);
  array[0] = 42;
  array[1]  = 0x12345678;

  let dataview = new DataView(array.buffer);
  for (let i = 0; i < 10000; ++i) {
    assertEquals(42, instance.exports.getInt32B(dataview, 0, 1));
  }

  time("Function finished\n")
})();

// TODO: Add test whether the typecheck fails, see e.g. CheckStackTrace in recognize-imports.js (line 55)
