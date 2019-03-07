// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-verify-heap

const kJSEmbeddingMaxGlobals = 1000000;

load("test/mjsunit/wasm/wasm-module-builder.js");

function addGlobals(builder, n) {
  for (let i = 0; i < n; i++) {
    builder.addGlobal(kWasmI32, true);
  }
}

(function TestGlobalsAtLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addGlobals(builder, kJSEmbeddingMaxGlobals);
  builder.instantiate();
})();

(function TestGlobalsAboveLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addGlobals(builder, kJSEmbeddingMaxGlobals + 1);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /globals count of 1000001 exceeds internal limit/);
})();
