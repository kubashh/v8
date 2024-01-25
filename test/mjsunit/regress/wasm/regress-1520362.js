// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.startRecGroup();
builder.addStruct([]);
builder.addStruct([]);
builder.addArray(kWasmI32, true);
builder.endRecGroup();
builder.startRecGroup();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.endRecGroup();
builder.startRecGroup();
builder.addType(makeSig([], []));
builder.endRecGroup();
builder.addMemory(16, 32);
builder.addTable(kWasmFuncRef, 2, 2, undefined)
builder.addActiveElementSegment(0, wasmI32Const(0), [[kExprRefFunc, 0, ], [kExprRefFunc, 1, ]], kWasmFuncRef);
builder.addTag(makeSig([], []));
// Generate function 1 (out of 2).
builder.addFunction(undefined, 3 /* sig */)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprRefNull, 0x02,  // ref.null
kExprI32Const, 0xf8, 0xee, 0xbf, 0xef, 0x78,  // i32.const
kExprI32Const, 0x9a, 0xbb, 0xfd, 0xbc, 0x03,  // i32.const
kExprI32Ior,  // i32.or
kExprI32Const, 0xed, 0xd0, 0x8b, 0xd2, 0x78,  // i32.const
kExprI32Const, 0xc6, 0x95, 0xd3, 0xe5, 0x00,  // i32.const
kExprI32ShrS,  // i32.shr_s
kExprI32ShrS,  // i32.shr_s
kExprI32Const, 0xbb, 0xa2, 0x8c, 0xa0, 0x02,  // i32.const
kGCPrefix, kExprArraySet, 0x02,  // array.set
kExprLoop, 0x7f,  // loop @39 i32
  kExprI32Const, 0x92, 0xc7, 0xa5, 0xc3, 0x00,  // i32.const
  kExprEnd,  // end @47
kExprEnd,  // end @48
]);
// Generate function 2 (out of 2).
builder.addFunction(undefined, 4 /* sig */)
  .addLocals(kWasmI64, 4).addLocals(kWasmI32, 5)
  .addBodyWithEnd([
// signature: v_v
// body:
kExprEnd,  // end @5
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
try {
  print(instance.exports.main(1, 2, 3));
} catch (e) {
  print('caught exception', e);
}
