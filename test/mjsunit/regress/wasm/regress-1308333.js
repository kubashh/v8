// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --experimental-wasm-gc

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([kWasmI64, kWasmExternRef, kWasmExternRef, kWasmExternRef, kWasmExternRef, kWasmExternRef, kWasmI32, kWasmExternRef, kWasmExternRef], [kWasmExternRef]));
builder.addMemory(16, 32, false);
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
kExprUnreachable,  // unreachable
kExprEnd  // end @353
]);
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
kExprUnreachable,  // unreachable
kExprEnd  // end @353
]);
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
kExprUnreachable,  // unreachable
kExprEnd  // end @353
]);
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
kExprUnreachable,  // unreachable
kExprEnd  // end @353
]);
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
kExprUnreachable,  // unreachable
kExprEnd  // end @353
]);
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
kExprUnreachable,  // unreachable
kExprEnd  // end @353
]);
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
kExprUnreachable,  // unreachable
kExprEnd  // end @353
]);
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
kExprUnreachable,  // unreachable
kExprEnd  // end @353
]);
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
kExprUnreachable,  // unreachable
kExprEnd  // end @353
]);
// Generate function 1 (out of 1).
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
kExprLocalGet, 2,
kExprEnd  // end @353
]).exportAs("main");
let instance = builder.instantiate();
assertEquals(4, instance.exports.main(2n, 2, 4, 4, 6, 6, 8, 8, 10));
