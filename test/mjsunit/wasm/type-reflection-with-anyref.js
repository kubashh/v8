// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-type-reflection --experimental-wasm-anyref

load('test/mjsunit/wasm/wasm-module-builder.js');

(function TestTableType() {
  let table = new WebAssembly.Table({initial: 1, element: "anyref"});
  let type = WebAssembly.Table.type(table);
  assertEquals(1, type.minimum);
  assertEquals("anyref", type.element);
  assertEquals(2, Object.getOwnPropertyNames(type).length);

  table = new WebAssembly.Table({initial: 2, maximum: 15, element: "anyref"});
  type = WebAssembly.Table.type(table);
  assertEquals(2, type.minimum);
  assertEquals(15, type.maximum);
  assertEquals("anyref", type.element);
  assertEquals(3, Object.getOwnPropertyNames(type).length);
})();

(function TestGlobalType() {
  let global = new WebAssembly.Global({value: "anyref", mutable: true});
  let type = WebAssembly.Global.type(global);
  assertEquals("anyref", type.value);
  assertEquals(true, type.mutable);
  assertEquals(2, Object.getOwnPropertyNames(type).length);

  global = new WebAssembly.Global({value: "anyref"});
  type = WebAssembly.Global.type(global);
  assertEquals("anyref", type.value);
  assertEquals(false, type.mutable);
  assertEquals(2, Object.getOwnPropertyNames(type).length);
})();

// This is an extension of "type-reflection.js/TestFunctionTableSetAndCall" to
// multiple table indexes. If --experimental-wasm-anyref is enabled by default
// this test case can supersede the other one.
(function TestFunctionMultiTableSetAndCall() {
  let builder = new WasmModuleBuilder();
  let fun1 = new WebAssembly.Function({parameters:[], results:["i32"]}, _ => 7);
  let fun2 = new WebAssembly.Function({parameters:[], results:["i32"]}, _ => 9);
  let fun3 = new WebAssembly.Function({parameters:[], results:["f64"]}, _ => 0);
  let table = new WebAssembly.Table({element: "anyfunc", initial: 2});
  let table_index0 = builder.addImportedTable("m", "table", 2);
  let table_index1 = builder.addTable(kWasmAnyFunc, 1).exportAs("tbl").index;
  let sig_index = builder.addType(kSig_i_v);
  table.set(0, fun1);
  builder.addFunction('main0', kSig_i_i)
      .addBody([
        kExprGetLocal, 0,
        kExprCallIndirect, sig_index, table_index0
      ])
      .exportFunc();
  builder.addFunction('main1', kSig_i_i)
      .addBody([
        kExprGetLocal, 0,
        kExprCallIndirect, sig_index, table_index1
      ])
      .exportFunc();
  let instance = builder.instantiate({ m: { table: table }});
  assertEquals(7, instance.exports.main0(0));
  table.set(1, fun2);
  assertEquals(9, instance.exports.main0(1));
  table.set(1, fun3);
  assertTraps(kTrapFuncSigMismatch, () => instance.exports.main0(1));
  assertTraps(kTrapFuncSigMismatch, () => instance.exports.main1(0));
  instance.exports.tbl.set(0, fun1);
  assertEquals(7, instance.exports.main1(0));
  instance.exports.tbl.set(0, fun2);
  assertEquals(9, instance.exports.main1(0));
  instance.exports.tbl.set(0, fun3);
  assertTraps(kTrapFuncSigMismatch, () => instance.exports.main1(0));
})();
