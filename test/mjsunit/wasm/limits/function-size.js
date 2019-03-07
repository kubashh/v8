// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-verify-heap

const kJSEmbeddingMaxFunctionSize = 7654321;

load("test/mjsunit/wasm/wasm-module-builder.js");

(function TestMaxFuncBodySize() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const type = builder.addType(kSig_v_v);
  const nops = kJSEmbeddingMaxFunctionSize - 2;
  const array = new Array(nops);
  for (let i = 0; i < nops; i++) array[i] = kExprNop;
  builder.addFunction(undefined, type).addBody(array);

  builder.instantiate();
})();

(function TestMaxFuncBodySizeOutOfBounds() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const type = builder.addType(kSig_v_v);
  const nops = kJSEmbeddingMaxFunctionSize - 2 + 100;
  const array = new Array(nops);
  for (let i = 0; i < nops; i++) array[i] = kExprNop;
  builder.addFunction(undefined, type).addBody(array)

  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /size 7654421 > maximum function size/);
})();
