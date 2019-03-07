// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const kJSEmbeddingMaxTables = 1;

load("test/mjsunit/wasm/wasm-module-builder.js");

function addTables(builder, n) {
  for (let i = 0; i < n; i++) {
    builder.addImportedTable("", "", 1, 1);
  }
}

(function TestTablesAtLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addTables(builder, kJSEmbeddingMaxTables);
  builder.toModule();
})();

(function TestTablesAboveLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addTables(builder, kJSEmbeddingMaxTables + 1);
  assertThrows(() => builder.toModule(), WebAssembly.CompileError,
               /At most one table is supported/);
})();
