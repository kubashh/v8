// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-type-reflection --experimental-wasm-gc

load("test/mjsunit/wasm/wasm-module-builder.js");

var instance = (function () {
  var builder = new WasmModuleBuilder();

  builder.addMemory(10000, 10000);

  builder.addFunction("wasm_x1", kSig_v_i)
    .addLocals(kWasmI32, 2)
    .addBody([kExprLoop, kWasmStmt,
              kExprLocalGet, 2, kExprLocalGet, 1, kExprF32LoadMem, 2, 0,
              ...wasmF32Const(0.0), kExprF32Max, kExprF32StoreMem, 2, 0,
              kExprLocalGet, 2, kExprI32Const, 4, kExprI32Add, kExprLocalSet, 2,
              kExprLocalGet, 1, kExprI32Const, 4, kExprI32Add, kExprLocalSet, 1,
              kExprLocalGet, 0, kExprI32Const, 4, kExprI32Sub, kExprLocalTee, 0,
              kExprI32Const, 3, kExprI32GtU, kExprBrIf, 0,
              kExprEnd])
    .exportFunc();

  return builder.instantiate({});
})();
assertTrue(!!instance);

{
  var time_before = performance.now();
  instance.exports.wasm_x1(500000000);
  var time_after = performance.now();
  var total_time = time_after - time_before;
  print("wasm_x1: " + total_time);
}
