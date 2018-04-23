// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-mut-global

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

(function TestBasic() {
  let global = new WebAssembly.Global({type: 'i32', value: 1});
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmI32);
  builder.addFunction("main", kSig_i_v)
    .addBody([kExprGetGlobal, 0])
    .exportAs("main");

  let main = builder.instantiate({mod: {g: global}}).exports.main;
  assertEquals(1, main());
})();

(function TestTypeMismatch() {
  let global = new WebAssembly.Global({type: 'f32', value: 1});
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmI32);
  builder.addFunction("main", kSig_i_v)
    .addBody([kExprGetGlobal, 0])
    .exportAs("main");

  assertThrows(() => builder.instantiate({mod: {g: global}}));
})();

(function TestImportI64AsNumber() {
  let builder = new WasmModuleBuilder();
  builder.addImportedGlobal("mod", "g", kWasmI64);
  assertThrows(() => builder.instantiate({mod: {g: 1234}}));
})();

(function TestImportMutableGlobal() {
  let globals = [
    [kWasmI32, 'i32'],
    [kWasmI64, 'i64'],
    [kWasmF32, 'f32'],
    [kWasmF64, 'f64'],
  ];
  function addGettersAndSetters(builder) {
    for (let [index, [type, name]] of globals.entries()) {
      if (name == 'i64') {
        builder.addFunction('geti64_hi', makeSig([], [kWasmI32]))
          .addBody([
            kExprGetGlobal, index,
            kExprI64Const, 32, kExprI64ShrU,
            kExprI32ConvertI64])
          .exportFunc();
        builder.addFunction('geti64_lo', makeSig([], [kWasmI32]))
          .addBody([kExprGetGlobal, index, kExprI32ConvertI64])
          .exportFunc();
        builder.addFunction("seti64", makeSig([kWasmI32, kWasmI32], []))
          .addBody([
            kExprGetLocal, 1, kExprI64UConvertI32,
            kExprGetLocal, 0, kExprI64UConvertI32,
            kExprI64Const, 32, kExprI64Shl,
            kExprI64Ior,
            kExprSetGlobal, index])
          .exportFunc();
      } else {
        builder.addFunction("get" + name, makeSig([], [type]))
          .addBody([kExprGetGlobal, index])
          .exportFunc();
        builder.addFunction("set" + name, makeSig([type], []))
          .addBody([kExprGetLocal, 0, kExprSetGlobal, index])
          .exportFunc();
      }
    }
  };

  let builder = new WasmModuleBuilder();
  for (let [type, name] of globals) {
    builder.addGlobal(type, true).exportAs(name);
  }
  addGettersAndSetters(builder);
  let inst1 = builder.instantiate();

  builder = new WasmModuleBuilder();
  for (let [type, name] of globals) {
    builder.addImportedGlobal("mod", name, type, true);
  }
  addGettersAndSetters(builder);
  let inst2 = builder.instantiate({
    mod: {
      i32: inst1.exports.i32,
      i64: inst1.exports.i64,
      f32: inst1.exports.f32,
      f64: inst1.exports.f64
    }
  });

  // i32, f32, f64
  for (let type of ['i32', 'f32', 'f64']) {
    let get1 = inst1.exports['get' + type];
    let get2 = inst2.exports['get' + type];
    let set1 = inst1.exports['set' + type];
    let set2 = inst2.exports['set' + type];

    assertEquals(get1(), get2(), type);
    set1(13579);
    assertEquals(13579, get2(), type);
    set2(97531);
    assertEquals(97531, get1(), type);
  }

  // i64
  assertEquals(inst1.exports.geti64_lo(), inst2.exports.geti64_lo());
  assertEquals(inst1.exports.geti64_hi(), inst2.exports.geti64_hi());
  inst1.exports.seti64(13579, 24680);
  assertEquals(13579, inst2.exports.geti64_hi());
  assertEquals(24680, inst2.exports.geti64_lo());
  inst2.exports.seti64(97531, 86420);
  assertEquals(97531, inst1.exports.geti64_hi());
  assertEquals(86420, inst1.exports.geti64_lo());
})();
