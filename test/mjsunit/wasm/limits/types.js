// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-verify-heap

const kJSEmbeddingMaxTypes = 1000000;

load("test/mjsunit/wasm/wasm-module-builder.js");

function addTypes(builder, n) {
  for (let i = 0; i < n; i++) {
      builder.addType(kSig_i_i);
  }
}

(function TestTypesAtLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addTypes(builder, kJSEmbeddingMaxTypes);
  builder.instantiate();
})();

(function TestTypesAboveLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addTypes(builder, kJSEmbeddingMaxTypes + 1);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /types count of 1000001 exceeds internal limit/);
})();
