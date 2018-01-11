// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

let module = (() => {
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, kV8MaxPages, false);
  builder.addFunction("grow_memory", kSig_i_i)
              .addBody([kExprGetLocal, 0, kExprGrowMemory, kMemoryZero])
    .exportFunc();
  builder.exportMemoryAs("memory");
  return builder.toModule();
})();

(function TestDetaching() {
  print("TestDetaching...");
  let instance = new WebAssembly.Instance(module);
  let growMem = instance.exports.grow_memory;
  let memory = instance.exports.memory;

  let b1 = memory.buffer;
  assertEquals(kPageSize, b1.byteLength);
})();
