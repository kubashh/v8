// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function exportTable64Getter(builder, table, type) {
  const table64_get_sig = makeSig([kWasmI64], [type]);
  builder.addFunction('table64_get', table64_get_sig)
      .addBody([
        kExprLocalGet, 0,
        kExprTableGet, table.index])
      .exportFunc();
}

function exportTable64Size(builder, table) {
  builder.addFunction('table64_size', kSig_l_v)
      .addBody([kNumericPrefix, kExprTableSize, table.index])
      .exportFunc();
}

function exportTable64Init(builder, table, passive) {
  builder.addFunction('table64_init', kSig_v_lii)
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kExprLocalGet, 2,
        kNumericPrefix, kExprTableInit, passive, table.index
      ])
      .exportFunc();
}

(function TestTable64Init() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_i_v, kNoSuperType, false);
  let f1 = builder.addFunction('f1', sig).addBody([kExprI32Const, 11]);
  let f2 = builder.addFunction('f2', sig).addBody([kExprI32Const, 22]);
  let f3 = builder.addFunction('f3', sig).addBody([kExprI32Const, 33]);
  let passive = builder.addPassiveElementSegment(
      [
        [kExprRefFunc, f1.index], [kExprRefFunc, f2.index],
        [kExprRefFunc, f3.index]
      ],
      wasmRefType(0));
  const table = builder.addTable64(kWasmAnyFunc, 5, 5).exportAs('table');

  exportTable64Init(builder, table, passive);
  exportTable64Getter(builder, table, kWasmAnyFunc);
  exportTable64Size(builder, table);

  let exports = builder.instantiate().exports;

  const dst = 1n;
  const src = 0;
  const size = 3;
  exports.table64_init(dst, src, size);
  assertEquals(5n, exports.table64_size());
  assertEquals(null, exports.table64_get(0n));
  assertEquals(11, exports.table64_get(dst)());
  assertEquals(22, exports.table64_get(dst+1n)());
  assertEquals(33, exports.table64_get(dst+2n)());
  assertEquals(null, exports.table64_get(4n));
})();
