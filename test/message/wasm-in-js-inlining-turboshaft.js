// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turboshaft-wasm-in-js-inlining
// Flags: --trace-turbo-inlining --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --no-always-sparkplug
// Concurrent inlining leads to additional traces.
// Flags: --no-stress-concurrent-inlining

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");
d8.file.execute("test/mjsunit/mjsunit.js");

function createWasmModule() {
  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI32, true);
  let global = builder.addGlobal(kWasmI32, true, false);

  builder.addFunction('empty', kSig_v_v).addBody([
  ]).exportFunc();

  builder.addFunction('nop', kSig_v_v).addBody([
    kExprNop
  ]).exportFunc();

  builder.addFunction('i32Const', kSig_i_v).addBody([
    ...wasmI32Const(42)
  ]).exportFunc();

  builder.addFunction('i32Add', kSig_i_ii).addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprI32Add,
  ]).exportFunc();

  builder.addFunction('localTee', kSig_i_i).addLocals(kWasmI32, 1).addBody([
    kExprLocalGet, 0,
    ...wasmI32Const(7),
    kExprLocalTee, 1,
    kExprI32Add,
    kExprLocalGet, 1,
    kExprI32Add,
  ]).exportFunc();

  builder.addFunction('globalSetGet', kSig_i_i).addBody([
    kExprLocalGet, 0,
    kExprGlobalSet, 0,
    kExprGlobalGet, 0,
    kExprGlobalGet, 0,
    kExprI32Add,
    kExprGlobalGet, 0,
    kExprI32Add,
  ]).exportFunc();

  builder.addFunction('createArray', makeSig([kWasmI32], [kWasmExternRef])).addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprArrayNewDefault, array,
    kGCPrefix, kExprExternConvertAny,
  ]).exportFunc();

  builder.addFunction('arrayLen', makeSig([kWasmExternRef], [kWasmI32])).addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprAnyConvertExtern,
    kGCPrefix, kExprRefCastNull, array,
    kGCPrefix, kExprArrayLen,
  ])
  .exportFunc();


  // All sorts of cases, which we don't inline (yet):

  // TODO(dlehmann,353475584): Do we want to support this?
  // We would need to extend the wrapper inlining to support this.
  builder.addFunction('multiValue', kSig_ii_v).addBody([
    ...wasmI32Const(3),
    ...wasmI32Const(7),
  ]).exportFunc();

  builder.addFunction('trapNoInline', kSig_v_v).addBody([
    kExprUnreachable,
  ]).exportFunc();

  builder.addFunction('brNoInline', kSig_i_v).addBody([
    ...wasmI32Const(42),
    // Need to use a `br`anch here, as `return` is actually optimized.
    kExprBr, 0,
  ]).exportFunc();

  let instance = builder.instantiate({});
  return instance.exports;
}

let wasmModule = createWasmModule();

function testOptimized(jsFunction) {
  print(`\nTest: ${jsFunction.name}`);

  %PrepareFunctionForOptimization(jsFunction);
  let resultUnopt = jsFunction();

  %OptimizeFunctionOnNextCall(jsFunction);
  let resultOpt = jsFunction();

  assertEquals(resultUnopt, resultOpt);
  return resultOpt;
}

(function TestInlining() {
  let empty = () => wasmModule.empty();
  testOptimized(empty);

  let nop = () => wasmModule.nop();
  testOptimized(nop);

  let i32Const = () => wasmModule.i32Const();
  testOptimized(i32Const);

  let i32Add = () => wasmModule.i32Add(23, 13);
  testOptimized(i32Add);

  let localTee = () => wasmModule.localTee(7);
  testOptimized(localTee);

  let globalSetGet = () => wasmModule.globalSetGet(7);
  testOptimized(globalSetGet);

  let createArray = () => wasmModule.createArray(42);
  let arrayExternRef = testOptimized(createArray);

  let arrayLen = () => wasmModule.arrayLen(arrayExternRef);
  testOptimized(arrayLen);

  let multiValue = () => wasmModule.multiValue();
  testOptimized(multiValue);

  let trapNoInline = () => {
    try {
      wasmModule.trapNoInline()
    } catch (e) {
      console.log(e)
    }
  };
  testOptimized(trapNoInline);

  let brNoInline = () => wasmModule.brNoInline();
  testOptimized(brNoInline);
})();
