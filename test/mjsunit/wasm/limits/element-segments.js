// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-verify-heap

const kJSEmbeddingMaxElementSegments = 10000000;

load("test/mjsunit/wasm/wasm-module-builder.js");

function addElementSegments(builder, n) {
  builder.setTableBounds(1, 1);
  const array = [];
  for (let i = 0; i < n; i++) {
      builder.addElementSegment(0, false, array, false);
  }
}

(function TestElementSegmentsAtLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addElementSegments(builder, kJSEmbeddingMaxElementSegments);
  builder.instantiate();
})();

(function TestElementSegmentsAboveLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  addElementSegments(builder, kJSEmbeddingMaxElementSegments + 1);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /element count of 1000001 exceeds internal limit/);
})();
