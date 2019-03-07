// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-verify-heap

const kJSEmbeddingMaxImports = 100000;

load("test/mjsunit/wasm/wasm-module-builder.js");

function addImports(builder, n) {
  const type = builder.addType(kSig_v_v);
  for (let i = 0; i < n; i++) {
      builder.addImport("", "", type);
  }
}

(function TestImportsAtLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addImports(builder, kJSEmbeddingMaxImports);
  builder.toModule();
})();

(function TestImportsAboveLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addImports(builder, kJSEmbeddingMaxImports + 1);
  assertThrows(() => builder.toModule(), WebAssembly.CompileError,
               /imports count of 100001 exceeds internal limit/);
})();
