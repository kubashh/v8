// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --liftoff --no-wasm-tier-up

load('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
builder
    .addMemory()
    .addFunction("main", kSig_i_v)
    .addLocals(kWasmI32, 1024)
    .addBody([kExprI32Const, 1,
              kExprI32Const, 2,
              kExprI32Add,
              kExprLocalTee, 0xfe, 0x04,
              kExprI32Const, 4,
              kExprI32Const, 5,
              kExprI32Add,
              kExprI32Const, 6,
              kExprI32Const, 7,
              kExprI32Add,
              kExprI32Const, 8,
              kExprI32Const, 9,
              kExprI32Add,
              kExprI32Const, 10,
              kExprI32Const, 11,
              kExprI32Add,
              kExprI32Const, 12,
              kExprI32Const, 13,
              kExprI32Add,
              kExprI32Const, 14,
              kExprI32Const, 15,
              kExprI32Add,
              kExprI32Const, 18,
              kExprI32Const, 19,
              kExprI32Add,
              kExprLocalSet, 0xfe, 0x03,
              kExprI32Const, 16,
              kExprI32Const, 17,
              kExprI32Add,
              kExprLocalGet, 0xfe, 0x03,
              kExprIf, kWasmStmt,
              kExprI32Const, 4,
              kExprLocalSet, 0xfe, 0x05,
              kExprEnd,
              kExprI32Add,
              kExprI32Add,
              kExprI32Add,
              kExprI32Add,
              kExprI32Add,
              kExprLocalGet, 0xfe, 0x04,
              kExprI32Add,
              kExprI32Add,
              kExprI32Add])
    .exportAs("main");
let instance = builder.instantiate();
assertEquals(instance.exports.main(), 153);
