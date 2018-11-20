// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-bigint

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

function assertTypeOfBigInt(x) {
  assertEquals(x.constructor, BigInt);
}

(function TestWasmI64ToJSBigInt() {
  var builder = new WasmModuleBuilder();

  builder
    .addFunction("fn", kSig_l_v) // () -> i64
    .addBody([
      kExprI64Const, 0x3,
    ])
    .exportFunc();

  var module = builder.instantiate();

  assertTypeOfBigInt(module.exports.fn());
  assertEquals(module.exports.fn(), 3n);
})();

(function TestWasmI64ToJSBigIntImportedFunc() {
  var builder = new WasmModuleBuilder();

  builder
    .addImport("a", "a", kSig_v_l) // i64 -> ()

  var func = builder
    .addFunction("", kSig_v_v) // () -> ()
    .addBody([
      kExprI64Const, 0x1,
      kExprCallFunction, 0
    ]);

  builder.addStart(func.index);

  builder.instantiate({
    a: {
      a(param) {
        assertTypeOfBigInt(param);
        assertEquals(param, 1n);
      },
    }
  });
})();

(function TestJSBigIntToWasmI64Global() {
  var builder = new WasmModuleBuilder();

  var a_global_index = builder
    .addImportedGlobal("mod", "a", kWasmI64)

  var b_global_index = builder
    .addImportedGlobal("mod", "b", kWasmI64);

  builder
    .addExportOfKind('a', kExternalGlobal, a_global_index)
    .addExportOfKind('b', kExternalGlobal, b_global_index);

  var module = builder.instantiate({
    mod: {
      a: 1n,
      b: 2n ** 63n,
    }
  });

  assertEquals(module.exports.a.value, 1n);
  assertEquals(module.exports.b.value, - (2n ** 63n));
})();

(function TestJSBigIntToWasmI64Identity() {
  var builder = new WasmModuleBuilder();

  builder
    .addFunction("f", kSig_l_l) // i64 -> i64
    .addBody([
      kExprGetLocal, 0x0,
    ])
    .exportFunc();

  var module = builder.instantiate();
  var f = module.exports.f;

  assertEquals(f(0n), 0n);
  assertEquals(f(-0n), -0n);
  assertEquals(f(123n), 123n);
  assertEquals(f(-123n), -123n);

  assertEquals(f("5"), 5n);

  assertThrows(() => f(5), TypeError);
})();

(function TestI64Global() {
  var argument = { "value": "i64", "mutable": true };
  var global = new WebAssembly.Global(argument);

  assertEquals(global.value, 0n); // initial value

  global.value = 123n;
  assertEquals(global.valueOf(), 123n);

  global.value = 2n ** 63n;
  assertEquals(global.valueOf(), - (2n ** 63n));
})();

(function TestInvalidValtypeGlobalErrorMessage() {
  var argument = { "value": "Nintendo64" };
  assertThrows(() => new WebAssembly.Global(argument));

  try {
    new WebAssembly.Global(argument);
  } catch (e) {
    assertContains("'value' must be", e.message);
    assertContains("i64", e.message);
  }
})();

(function TestGlobalI64SetWrongType() {
  var argument = { "value": "i64", "mutable": true };
  var global = new WebAssembly.Global(argument);

  assertThrows(() => global.value = 1, TypeError);
})();

(function TestFuncParamF64PassingBigInt() {
  var builder = new WasmModuleBuilder();

  builder
      .addFunction("f", kSig_v_d) // f64 -> ()
      .addBody([])
      .exportFunc();

  var module = builder.instantiate();

  assertThrows(() => module.exports.f(123n), TypeError);
})();
