// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const kJSEmbeddingMaxMemoryPages = 65536;

load("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeclaredMemoryPagesAtLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addMemory(1, kJSEmbeddingMaxMemoryPages);
  builder.instantiate();
})();

(function TestDeclaredMemoryPagesAboveLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addMemory(1, kJSEmbeddingMaxMemoryPages + 1);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError,
               /maximum memory size \(65537 pages\) is larger/);
})();

(function TestImportedMemoryPagesAtLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory("mod", "mem", 1, kJSEmbeddingMaxMemoryPages);
  builder.toModule();
})();

(function TestImportedMemoryPagesAboveLimit() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory("mod", "mem", 1, kJSEmbeddingMaxMemoryPages + 1);
  assertThrows(() => builder.toModule(), WebAssembly.CompileError,
               /maximum memory size \(65537 pages\) is larger/);
})();
