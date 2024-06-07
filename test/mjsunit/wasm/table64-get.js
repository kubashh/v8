// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestTable64GetFuncRef() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmAnyFunc, 10).exportAs('table');
  const f1 = builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 11]);
  const offset = 3;
  builder.addActiveElementSegment(0, wasmI64Const(offset), [f1.index]);

  const table64_get_sig = makeSig([kWasmI64], [kWasmAnyFunc]);
  builder.addFunction('table64_get', table64_get_sig)
      .addBody([kExprLocalGet, 0, kExprTableGet, table.index])
      .exportFunc();

  let exports = builder.instantiate().exports;

  assertEquals(11, exports.table64_get(BigInt(offset))());
})();

(function TestTable64GetAnyRef() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmAnyRef, 10, 15).exportAs('table');
  const struct = builder.addStruct([makeField(kWasmI32, true)])
  const offset = 4;
  builder.addActiveElementSegment(
      0, wasmI64Const(offset),
      [[kExprI32Const, 23, kGCPrefix, kExprStructNew, struct]], kWasmAnyRef);

  builder.addFunction('getField', makeSig([kWasmAnyRef], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,
        kGCPrefix, kExprRefCast, struct,
        kGCPrefix, kExprStructGet, struct, 0
      ])
      .exportFunc();

  const table64_get_sig = makeSig([kWasmI64], [kWasmAnyRef]);
  builder.addFunction('table64_get', table64_get_sig)
      .addBody([kExprLocalGet, 0, kExprTableGet, table.index])
      .exportFunc();

  let exports = builder.instantiate().exports;

  assertEquals(23, exports.getField(exports.table64_get(BigInt(offset))));
})();

(function TestTable64GetExternRef() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmExternRef, 10).exportAs('table');

  const table64_get_sig = makeSig([kWasmI64], [kWasmExternRef]);
  builder.addFunction('table64_get', table64_get_sig)
      .addBody([kExprLocalGet, 0, kExprTableGet, table.index])
      .exportFunc();

  let exports = builder.instantiate().exports;

  assertEquals(null, exports.table64_get(8n));
})();

(function TestTable64GetWrongType() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmAnyFunc, 10).exportAs('table');
  const f1 = builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 11]);
  const offset = 3;
  builder.addActiveElementSegment(0, wasmI64Const(offset), [f1.index]);

  // Table64 expects kWasmI64 as the index to the table.
  const table32_get_sig = makeSig([kWasmI32], [kWasmAnyFunc]);
  builder.addFunction('table32_get', table32_get_sig)
      .addBody([kExprLocalGet, 0, kExprTableGet, table.index])
      .exportFunc();

  assertThrows(() => builder.toModule(), WebAssembly.CompileError);
})();
