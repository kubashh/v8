// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

(function exportImmutableGlobal() {
  var builder = new WasmModuleBuilder();
  const kInitI32Value = 4711;
  const kInitF32Value = Math.fround(3.14);
  const kInitF64Value = 1/7;
  builder.addGlobal(kWasmI32, false).exportAs('i32_noinit');
  builder.addGlobal(kWasmI32, false).exportAs('i32').init = kInitI32Value;
  builder.addGlobal(kWasmF32, false).exportAs('f32').init = kInitF32Value;
  builder.addGlobal(kWasmF64, false).exportAs('f64').init = kInitF64Value;
  var module = builder.instantiate();

  assertEquals("number", typeof module.exports.i32_noinit);
  assertEquals(0, module.exports.i32_noinit);
  assertEquals("number", typeof module.exports.i32);
  assertEquals(kInitI32Value, module.exports.i32);
  assertEquals("number", typeof module.exports.f32);
  assertEquals(kInitF32Value, module.exports.f32);
  assertEquals("number", typeof module.exports.f64);
  assertEquals(kInitF64Value, module.exports.f64);
})();

(function cannotExportMutableGlobal() {
  var builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, true).exportAs('g');
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError);
})();

(function cannotExportI64Global() {
  var builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI64, false).exportAs('g');
  assertThrows(() => builder.instantiate(), WebAssembly.LinkError);
})();
