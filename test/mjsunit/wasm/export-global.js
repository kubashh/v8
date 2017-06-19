// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

(function exportImmutableGlobal() {
  var builder = new WasmModuleBuilder();
  const kInitValue = 4711;
  builder.addGlobal(kWasmI32, false).exportAs('g');
  builder.addGlobal(kWasmI32, false).exportAs('h').init = kInitValue;
  var module = builder.instantiate();

  assertEquals("number", typeof module.exports.g);
  assertEquals(0, module.exports.g);
  assertEquals("number", typeof module.exports.h);
  assertEquals(kInitValue, module.exports.h);
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
