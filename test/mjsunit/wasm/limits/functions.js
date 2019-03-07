// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-verify-heap

const kJSEmbeddingMaxFunctions = 1000000;

load("test/mjsunit/wasm/wasm-module-builder.js");

function addFunctions(builder, n) {
  const type = builder.addType(kSig_v_v);
  const body = [];
  for (let i = 0; i < n; i++) {
      builder.addFunction(/*name=*/ undefined, type).addBody(body);
  }
}

(function TestFunctionsAtLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addFunctions(builder, kJSEmbeddingMaxFunctions);
  builder.instantiate();
})();

(function TestFunctionsAboveLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addFunctions(builder, kJSEmbeddingMaxFunctions + 1);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /functions count of 1000001 exceeds internal limit/);
})();
