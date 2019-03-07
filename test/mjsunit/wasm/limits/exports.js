// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-verify-heap

const kJSEmbeddingMaxExports = 100000;

load("test/mjsunit/wasm/wasm-module-builder.js");

function addExports(builder, n) {
  const type = builder.addType(kSig_v_v);
  const f = builder.addFunction(/*name=*/ undefined, type);
  f.addBody([]);
  for (let i = 0; i < n; i++) {
    builder.addExport("f" + i, f.index);
  }
}

(function TestExportsAtLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addExports(builder, kJSEmbeddingMaxExports);
  builder.instantiate();
})();

(function TestExportsAboveLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addExports(builder, kJSEmbeddingMaxExports + 1);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /exports count of 100001 exceeds internal limit/);
})();
