// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function exportTable64CallIndirect(builder, table, param_l, param_r, it_tail_call) {
  let sig = builder.addType(kSig_i_iii);
  builder.addFunction('table64_callindirect', kSig_i_i)
      .addBody([
        ...wasmI32Const(param_l),
        ...wasmI32Const(param_r),
        kExprLocalGet, 0,
        ...wasmI64Const(0),
        it_tail_call ? kExprReturnCallIndirect : kExprCallIndirect,
        sig, table.index
      ])
      .exportFunc();
}

function test(is_tail_call) {
  const builder = new WasmModuleBuilder();

  let js_function = function(a, b, c) {
    return c ? a : b;
  };
  let callee = builder.addImport('m', 'f', kSig_i_iii);
  let table = builder.addTable64(kWasmFuncRef, 10, 10);
  builder.addActiveElementSegment(table.index, wasmI64Const(0), [callee]);

  let left = -2;
  let right = 3;
  exportTable64CallIndirect(builder, table, left, right, is_tail_call);
  let instance = builder.instantiate({m: {f: js_function}});

  assertEquals(left, instance.exports.table64_callindirect(1));
  assertEquals(right, instance.exports.table64_callindirect(0));
}

(function TestTable64CallIndirect() {
  print(arguments.callee.name);
  test(false);
})();

(function TestTable64ReturnCallIndirect() {
  print(arguments.callee.name);
  test(true);
})();
