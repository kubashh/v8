// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function exportTable64Getter(builder, table, type) {
  const table64_get_sig = makeSig([kWasmI64], [type]);
  builder.addFunction('table64_get', table64_get_sig)
      .addBody(
        [kExprLocalGet, 0,
         kExprTableGet, table.index])
      .exportFunc();
}

function exportTable64Setter(builder, table, type) {
  const table64_set_sig = makeSig([kWasmI64, type], []);
  builder.addFunction('table64_set', table64_set_sig)
      .addBody(
        [kExprLocalGet, 0,
         kExprLocalGet, 1,
         kExprTableSet, table.index])
      .exportFunc();
}

(function TestTable64SetFuncRef() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmAnyFunc, 10).exportAs('table');
  builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 11]).exportFunc();

  exportTable64Getter(builder, table, kWasmAnyFunc);
  exportTable64Setter(builder, table, kWasmAnyFunc);

  let exports = builder.instantiate().exports;

  const offset = 5n;
  assertEquals(null, exports.table64_get(offset));
  exports.table64_set(offset, exports.f);
  assertSame(exports.f, exports.table64_get(offset));
})();

(function TestTable64SetExternRef() {
    print(arguments.callee.name);
    const builder = new WasmModuleBuilder();
    const table = builder.addTable64(kWasmExternRef, 10, 20).exportAs('table');

    exportTable64Getter(builder, table, kWasmExternRef);
    exportTable64Setter(builder, table, kWasmExternRef);

    let exports = builder.instantiate().exports;

    const offset = 1n;
    const dummy_ref = {foo : 1, bar : 3};
    assertEquals(null, exports.table64_get(offset));
    exports.table64_set(offset, dummy_ref);
    assertSame(dummy_ref, exports.table64_get(offset));
  })();

(function TestTable64SetFuncRefWrongType() {
    print(arguments.callee.name);
    const builder = new WasmModuleBuilder();
    const table = builder.addTable64(kWasmAnyFunc, 10).exportAs('table');
    builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 11]).exportFunc();

    // Table64 expects kWasmI64 as the index to the table.
    const table64_set_sig = makeSig([kWasmI32, kWasmAnyFunc], []);
    builder.addFunction('table64_set', table64_set_sig)
        .addBody(
          [kExprLocalGet, 0,
           kExprLocalGet, 1,
           kExprTableSet, table.index])
        .exportFunc();

    assertThrows(() => builder.toModule(), WebAssembly.CompileError);
  })();
