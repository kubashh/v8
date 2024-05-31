// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestTable64Get() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmAnyFunc, 10).exportAs('table');
  const f1 = builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 11]);
  const offset = 3;
  builder.addActiveElementSegment(0, wasmI32Const(offset), [f1.index]);

  const table64_get_sig = makeSig([kWasmI64], [kWasmAnyFunc]);
  builder.addFunction('table64_get', table64_get_sig)
      .addBody([kExprLocalGet, 0, kExprTableGet, table.index])
      .exportFunc();

  let exports = builder.instantiate().exports;

  assertEquals(11, exports.table64_get(BigInt(offset))());
})();

(function TestTable64GetWrongType() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmAnyFunc, 10).exportAs('table');
  const f1 = builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 11]);
  const offset = 3;
  builder.addActiveElementSegment(0, wasmI32Const(offset), [f1.index]);

  const table32_get_sig = makeSig([kWasmI32], [kWasmAnyFunc]);
  builder.addFunction('table32_get', table32_get_sig)
      .addBody([kExprLocalGet, 0, kExprTableGet, table.index])
      .exportFunc();

  assertThrows(() => builder.toModule(), WebAssembly.CompileError);
})();
