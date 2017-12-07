// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --trace-wasm-decoder

load('test/mjsunit/wasm/wasm-constants.js');
load('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addFunction('fib32', kSig_i_i)
    .addLocals({i32_count: 2})
    .addBody([
      // n in local 0 (parameter), a in local 1, b in local 2.
      // b := 1
      kExprI32Const, 1, kExprSetLocal, 2, kExprLoop, kWasmI32, kExprBlock,
      kWasmStmt,
      // Return a if n == 0.
      kExprGetLocal, 0, kExprBrIf, 0, kExprGetLocal, 1, kExprReturn, kExprEnd,
      // Compute a+b, leave it on the stack.
      kExprGetLocal, 1, kExprGetLocal, 2, kExprI32Add,
      // a := b
      kExprGetLocal, 2, kExprSetLocal, 1,
      // b := a+b
      kExprSetLocal, 2,
      // --n
      kExprGetLocal, 0, kExprI32Const, 1, kExprI32Sub, kExprSetLocal, 0,
      // continue
      kExprBr, 0, kExprEnd
    ])
    .exportFunc();
builder.instantiate();
