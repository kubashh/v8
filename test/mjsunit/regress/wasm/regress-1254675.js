// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// Feature detect to see if SIMD is supported.
const builder = new WasmModuleBuilder();
  builder.addType(makeSig([], [kWasmI32]));
  builder.addFunction(undefined, 0).addBodyWithEnd([
    kExprI32Const, 1,
    kSimdPrefix, kExprI32x4Splat,
    kSimdPrefix, kExprI32x4ExtractLane, 1,
    kExprEnd,
  ]);

var supports_simd = true;

try {
  builder.instantiate();
} catch (e) {
  if (e instanceof WebAssembly.CompileError) {
    supports_simd = false;
  }
}

if (!supports_simd) {
  // Test case manually reduced from https://crbug.com/1254675.
  // This exercises a bug where we are missing checks for SIMD hardware support
  // when a function has a v128 parameter but doesn't use any SIMD instructions.
  (function() {
    const builder = new WasmModuleBuilder();
      builder.addType(kSig_i_s);
      builder.addFunction(undefined, 0).addBodyWithEnd([kExprUnreachable, kExprEnd]);

    assertThrows(() => builder.instantiate());
  }());

  // Additional test case to verify that a declared v128 local traps.
  (function() {
    const builder = new WasmModuleBuilder();
      builder.addType(kSig_i_i);
      builder.addFunction(undefined, 0).addBodyWithEnd([kExprUnreachable, kExprEnd])
             .addLocals('v128', 1);

    assertThrows(() => builder.instantiate());
  }());
}
