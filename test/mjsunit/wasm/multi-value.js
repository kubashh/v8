// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-mv

load("test/mjsunit/wasm/wasm-module-builder.js");

(function MultiWasmToJSReturnTest() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig_fi_if = makeSig([kWasmI32, kWasmF32], [kWasmF32, kWasmI32]);

  builder.addFunction("swap", kSig_ii_i)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 0])
    .exportAs("swap");
 /* builder.addFunction("addsubmul", kSig_iii_i)
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 0,
        kExprI32Add,
        kExprLocalGet, 0,
        kExprLocalGet, 0,
        kExprI32Sub,
        kExprLocalGet, 0,
        kExprLocalGet, 0,
        kExprI32Mul])
    .exportAs("addsubmul");
*/
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module);
  assertEquals(instance.exports.swap(0), [0, 0]);
  assertEquals(instance.exports.swap(2), [2, 2]);
 // assertEquals(instance.exports.addsubmul(4), [8, 0, 16]);
 // assertEquals(instance.exports.addsubmul(5), [10, 0, 25]);
})();

