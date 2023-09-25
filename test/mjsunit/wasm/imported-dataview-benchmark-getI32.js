// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --allow-natives-syntax

// Access elements using Turboshaft DataView.prototype.GetInt32().

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const start = performance.now();
function time(name) {
  const ms_since_start = (performance.now() - start).toFixed(1).padStart(7);
  print(`[${ms_since_start}] ${name}`);
}

let kRefExtern = wasmRefType(kWasmExternRef);

let kSig_i_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI32]);

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

(function TestGetInt32() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction("getInt32B", kSig_i_rii).exportFunc()
    .addLocals(kWasmI32, 2)   // two locals: loop counter and temp variable
    .addBody([
      // counter = 0;
      kExprI32Const, 0, kExprLocalSet, 3,
      // for_block
      kExprBlock, kWasmVoid,
        kExprLoop, kWasmVoid,
          // If counter == 5000, then jump after for_block.
          kExprLocalGet, 3, kExprI32Const, ...wasmSignedLeb(5000), kExprI32Eq, kExprBrIf, 1,
          // counter++;
          kExprLocalGet, 3, kExprI32Const, 1, kExprI32Add,
          kExprLocalSet, 3,
          // Set up and call `GetInt32()`.
          kExprLocalGet, 0,
          kExprLocalGet, 1,
          kExprLocalGet, 2,
          kExprCallFunction, kDataViewGetInt32C,
          // Set temp local variable.
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

  let result;

  let dataview = new DataView(array.buffer);
  for (let i = 0; i < 10000; ++i) {
    result += instance.exports.getInt32B(dataview, i % 4, 1);
  }

  time("Function finished\n");
  return result;
})();

// Time: 66.4 ms.
