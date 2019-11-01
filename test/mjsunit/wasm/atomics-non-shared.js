// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-threads --wasm-atomics-on-non-shared-memory

load("test/mjsunit/wasm/wasm-module-builder.js");

(function TestCompileAtomicOpUndefinedShared() {
  let memory = new WebAssembly.Memory({initial: 0, maximum: 10});
  let builder = new WasmModuleBuilder();
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kAtomicPrefix,
      kExprI32AtomicAdd, 2, 0]);
  builder.addImportedMemory("m", "imported_mem");
  let module = new WebAssembly.Module(builder.toBuffer());
})();
