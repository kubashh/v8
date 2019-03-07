// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const kJSEmbeddingMaxDataSegments = 100000;

load("test/mjsunit/wasm/wasm-module-builder.js");

function addDataSegments(builder, n) {
    const data = [];
    builder.addMemory(1, 1);
    for (let i = 0; i < n; i++) {
        builder.addDataSegment(0, data);
    }
}

(function TestDataSegmentsAtLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addDataSegments(builder, kJSEmbeddingMaxDataSegments);
  builder.instantiate();
})();

(function TestDataSegmentsAboveLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addDataSegments(builder, kJSEmbeddingMaxDataSegments + 1);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /data segments count of 100001 exceeds internal limit/);
})();
