// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --allow-natives-syntax --turbofan
// Flags: --no-always-turbofan --no-always-sparkplug --expose-gc
// Flags: --experimental-wasm-js-inlining

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function testOptimized(run, fctToOptimize) {
  fctToOptimize = fctToOptimize ?? run;
  %PrepareFunctionForOptimization(fctToOptimize);
  for (let i = 0; i < 10; ++i) {
    run();
  }
  %OptimizeFunctionOnNextCall(fctToOptimize);
  run();
  assertOptimized(fctToOptimize);
}

let wasm = (function createWasmModule() {
  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI32, true);

  builder.addFunction('createArray', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewDefault, array,
      kGCPrefix, kExprExternExternalize,
    ])
    .exportFunc();

  builder.addFunction('arrayLen', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCastNull, array,
      kGCPrefix, kExprArrayLen,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  return instance.exports;
})();

(function TestNestedInlining() {
  print(arguments.callee.name);

  let array42 = wasm.createArray(42);

  // Compile and optimize wasm function inlined into JS (not trapping).
  let testLen = (expected, array) => {
    let fct = () => assertSame(expected, wasm.arrayLen(array));
    // let obj = { fct };
    %PrepareFunctionForOptimization(fct);
    return /*obj.*/fct(/*expected, array*/); // TODO
  };
  testOptimized(() => testLen(42, array42), testLen);

  // Cause gc on next allocation.
  // Note: Neither testLen nor wasm.arrayLen allocate prior to the trap.
  // The first allocation is the wasm trap being created.
  try {
    // %SimulateNewspaceFull();
    testLen(42, null);
    assertUnreachable();
  } catch (e) {
    assertMatches(/dereferencing a null pointer/, e.stack);
  }
})();

// (function TestNestedInliningExtraArguments() {
//   print(arguments.callee.name);
//   let array42 = wasm.createArray(42);
//   // Compile and optimize wasm function inlined into JS (not trapping).
//   let testLen = (expected, array) => {
//     let fct = () => assertSame(expected, wasm.arrayLen(array));
//     %PrepareFunctionForOptimization(fct);
//     // fct doesn't take any arguments, so these are all "superfluous".
//     return fct(array, 1, undefined, {"test": "object"});
//   };
//   testOptimized(() => testLen(42, array42), testLen);
//   // Cause trap.
//   try {
//     testLen(42, null);
//     assertUnreachable();
//   } catch (e) {
//     assertMatches(/dereferencing a null pointer/, e.stack);
//   }
// })();

// (function TestNestedInliningWithReceiverFromOuterScope() {
//   print(arguments.callee.name);
//   let array42 = wasm.createArray(42);
//   // Compile and optimize wasm function inlined into JS (not trapping).
//   let fct = (expected, array) => assertSame(expected, wasm.arrayLen(array));
//   %PrepareFunctionForOptimization(fct);
//   let obj = { fct };
//   let testLen = (expected, array) => {
//     return obj.fct(expected, array);
//   };
//   testOptimized(() => testLen(42, array42), testLen);
//   // Cause trap.
//   try {
//     testLen(42, null);
//     assertUnreachable();
//   } catch (e) {
//     assertMatches(/dereferencing a null pointer/, e.stack);
//   }
// })();

// (function TestNestedInliningWithReceiverAsInlineObject() {
//   print(arguments.callee.name);
//   let array42 = wasm.createArray(42);
//   // Compile and optimize wasm function inlined into JS (not trapping).
//   let testLen = (expected, array) => {
//     let fct = () => assertSame(expected, wasm.arrayLen(array));
//     %PrepareFunctionForOptimization(fct);
//     let obj = { fct };
//     return obj.fct();
//   };
//   testOptimized(() => testLen(42, array42), testLen);
//   // Cause trap.
//   try {
//     testLen(42, null);
//     assertUnreachable();
//   } catch (e) {
//     assertMatches(/dereferencing a null pointer/, e.stack);
//   }
// })();
