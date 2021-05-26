// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --enable-testing-opcode-in-wasm --nowasm-tier-up

load("test/mjsunit/wasm/wasm-module-builder.js");

const kNopForTestingUnsupportedInLiftoff = 0x16;

var instance = (function () {
  var builder = new WasmModuleBuilder();
  builder.addMemory(1, 1, false /* exported */);

  var sig_index = builder.addType(makeSig(
      [
        kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32,
        kWasmI32,
      ],
    [kWasmI32]));

  var zero = builder.addFunction("zero", kSig_i_i);
  var one = builder.addFunction("one", sig_index);
  var two = builder.addFunction("two", kSig_v_i);
  var nop = builder.addFunction("nop", kSig_v_v).addBody([]);

  zero.addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, 0]);

  one.addBody([
    kNopForTestingUnsupportedInLiftoff,
    kExprLocalGet, 7,
    kExprCallFunction, zero.index]);

  two.addBody([
      kExprI64Const, 0x80, 0x80, 0x80, 0x80, 0x10,
      kExprI64Const, 0x80, 0x80, 0x80, 0x80, 0x10,
      kExprCallFunction, nop.index,
      kExprDrop,
      kExprDrop,
      kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add,
      kExprLocalGet, 0, kExprI32Const, 2, kExprI32Add,
      kExprLocalGet, 0, kExprI32Const, 3, kExprI32Add,
      kExprLocalGet, 0, kExprI32Const, 4, kExprI32Add,
      kExprLocalGet, 0, kExprI32Const, 5, kExprI32Add,
      kExprLocalGet, 0, kExprI32Const, 6, kExprI32Add,
      kExprLocalGet, 0, kExprI32Const, 7, kExprI32Add,
      kExprLocalGet, 0, kExprI32Const, 8, kExprI32Add,
      kExprCallFunction, one.index,
      kExprDrop,
    ]).exportFunc();

  return builder.instantiate({});
})();

instance.exports.two(34)
