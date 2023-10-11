// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addStruct([]);
builder.addStruct([makeField(wasmRefType(kWasmFuncRef), false)], 0);
builder.startRecGroup();
builder.addStruct([makeField(kWasmI32, false), makeField(kWasmI32, false), makeField(kWasmI32, false)], 0);
builder.endRecGroup();
builder.addArray(kWasmI32, true);
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.startRecGroup();
builder.addType(makeSig([], []));
builder.endRecGroup();
builder.addMemory(16, 32);
builder.addTable(kWasmFuncRef, 2, 2, undefined)
builder.addActiveElementSegment(0, wasmI32Const(0), [[kExprRefFunc, 0, ], [kExprRefFunc, 1, ]], kWasmFuncRef);
builder.addTag(makeSig([], []));
// Generate function 1 (out of 2).
builder.addFunction(undefined, 4 /* sig */)
  .addLocals(kWasmI32, 2).addLocals(wasmRefType(kWasmFuncRef), 1).addLocals(wasmRefType(5), 1).addLocals(wasmRefType(0), 1).addLocals(kWasmS128, 1).addLocals(kWasmEqRef, 1).addLocals(kWasmF32, 1).addLocals(wasmRefNullType(0), 1).addLocals(kWasmF32, 1).addLocals(wasmRefNullType(2), 1)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprRefNull, 0x04,  // ref.null
kExprRefAsNonNull,  // ref.as_non_null
kGCPrefix, kExprRefCastNull, 0x70,  // ref.cast null
kGCPrefix, kExprRefCastNull, 0x04,  // ref.cast null
kGCPrefix, kExprRefCast, 0x04,  // ref.cast
kGCPrefix, kExprStructNew, 0x01,  // struct.new
kGCPrefix, kExprStructGet, 0x01, 0x00,  // struct.get
kGCPrefix, kExprStructNew, 0x01,  // struct.new
kGCPrefix, kExprStructGet, 0x01, 0x00,  // struct.get
kGCPrefix, kExprStructNew, 0x01,  // struct.new
kGCPrefix, kExprStructGet, 0x01, 0x00,  // struct.get
kExprLocalSet, 0x05,  // local.set
kExprRefNull, 0x70,  // ref.null
kGCPrefix, kExprRefCastNull, 0x05,  // ref.cast null
kExprRefAsNonNull,  // ref.as_non_null
kGCPrefix, kExprRefCastNull, 0x04,  // ref.cast null
kGCPrefix, kExprRefCast, 0x05,  // ref.cast
kExprLocalSet, 0x06,  // local.set
kGCPrefix, kExprStructNewDefault, 0x00,  // struct.new_default
kExprRefAsNonNull,  // ref.as_non_null
kGCPrefix, kExprRefCastNull, 0x6e,  // ref.cast null
kGCPrefix, kExprRefCastNull, 0x6c,  // ref.cast null
kGCPrefix, kExprRefCast, 0x00,  // ref.cast
kExprLocalSet, 0x07,  // local.set
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, kExprI32x4DotI16x8S, 0x01,  // i32x4.dot_i16x8_s
kSimdPrefix, kExprF64x2Lt,  // f64x2.lt
kSimdPrefix, kExprI32x4BitMask, 0x01,  // i32x4.bitmask
kExprEnd,  // end @176
]);
// Generate function 2 (out of 2).
builder.addFunction(undefined, 5 /* sig */)
  .addLocals(wasmRefNullType(kWasmNullFuncRef), 1).addLocals(wasmRefType(5), 1).addLocals(kWasmI32, 1).addLocals(wasmRefType(kWasmI31Ref), 1).addLocals(wasmRefType(kWasmAnyRef), 1).addLocals(wasmRefType(kWasmExternRef), 1).addLocals(kWasmS128, 1).addLocals(kWasmI32, 1).addLocals(kWasmF64, 1)
  .addBodyWithEnd([
// signature: v_v
// body:
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, kExprF64x2Lt,  // f64x2.lt
kSimdPrefix, kExprI32x4BitMask, 0x01,  // i32x4.bitmask
kExprTableGet, 0x00,  // table.get
kGCPrefix, kExprRefCast, 0x05,  // ref.cast
kExprLocalSet, 0x01,  // local.set
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, kExprI16x8BitMask, 0x01,  // i16x8.bitmask
kGCPrefix, kExprRefI31,  // ref.i31
kExprLocalSet, 0x03,  // local.set
kExprRefNull, 0x6e,  // ref.null
kGCPrefix, kExprRefCast, 0x6a,  // ref.cast
kExprLocalSet, 0x04,  // local.set
kExprRefNull, 0x6f,  // ref.null
kExprRefAsNonNull,  // ref.as_non_null
kExprLocalSet, 0x05,  // local.set
kExprEnd,  // end @66
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertTraps(kTrapNullDereference, () => instance.exports.main(1, 2, 3));
