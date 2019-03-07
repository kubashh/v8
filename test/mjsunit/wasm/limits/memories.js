// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const kJSEmbeddingMaxMemories = 1;

load("test/mjsunit/wasm/wasm-module-builder.js");

function addMemories(builder, n) {
  for (let i = 0; i < n; i++) {
    builder.addImportedMemory("", "", 1, 1);
  }
}

(function TestMemoriesAtLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addMemories(builder, kJSEmbeddingMaxMemories);
  builder.toModule();
})();

(function TestMemoriesAboveLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addMemories(builder, kJSEmbeddingMaxMemories + 1);
  assertThrows(() => builder.toModule(), WebAssembly.CompileError,
               /At most one memory is supported/);
})();
