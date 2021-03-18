// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc

// During Turbofan optimizations, when a TrapIf/Unless node is found to always
// trap, its uses need to be marked as dead. However, in the case that one of
// these uses is a Merge or Loop node, only the input of the Merge/Loop that
// corresponds to the trap should be marked as dead.

load('test/mjsunit/wasm/wasm-module-builder.js');

var builder = new WasmModuleBuilder();

builder.addStruct([makeField(kWasmI32, true)]);

builder.addFunction('test', makeSig([wasmOptRefType(0)], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprRefIsNull,
      kExprIf, kWasmI32,
        kExprLocalGet, 0,
        kGCPrefix, kExprStructGet, 0, 0,
      kExprElse,
        kExprI32Const, 42,
      kExprEnd
    ])
    .exportFunc();
builder.instantiate();
