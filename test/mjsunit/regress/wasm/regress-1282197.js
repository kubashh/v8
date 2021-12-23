// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --turbo-force-mid-tier-regalloc

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32, false);
builder.addFunction(undefined, kSig_i_iii)
  .addBody([
    ...wasmF64Const(5),  // f64.const
    kExprF64Neg,  // f64.neg
    kNumericPrefix, kExprI64SConvertSatF64,  // i64.trunc_sat_f64_s
    ...wasmF64Const(0),  // f64.const
    ...wasmI32Const(3),  // i32.const
    kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
    kSimdPrefix, kExprF32x4DemoteF64x2Zero,  // f32x4.demote_f64x2_zero
    kSimdPrefix, kExprI32x4BitMask, 0x01,  // i32x4.bitmask
    kExprF64SConvertI32,  // f64.convert_i32_s
    kExprF64Mul,  // f64.mul
    kNumericPrefix, kExprI64SConvertSatF64,  // i64.trunc_sat_f64_s
    kExprI64LtU,  // i64.lt_u
]);
builder.toModule();
