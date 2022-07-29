// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-lazy-compilation --experimental-wasm-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Regression test to make sure big functions don't get compiled eagerly when
// lazy compilation is enabled. When this test was created, the compilation
// pipeline was not able yet to have parts of the module compiled eagerly and
// parts of the module compiled lazily.
const builder = new WasmModuleBuilder();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addMemory(16, 32, false);
kSizeLimit = 500 * 1024;
let code = [kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0, 0];
while (code.length < kSizeLimit) {
  code = code.concat(code);
}

code = code.concat([kExprI32Const, 12]);

builder.addFunction(undefined, 0 /* sig */)
  .addBody(code).exportAs("main");
let instance = builder.instantiate();
assertEquals(12, instance.exports.main(4, 8, 16));
