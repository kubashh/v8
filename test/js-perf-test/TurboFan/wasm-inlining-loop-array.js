// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --experimental-wasm-js-inlining

d8.file.execute('../../mjsunit/wasm/wasm-module-builder.js');

(function() {
  // Compile and instantiate wasm.
  let builder = new WasmModuleBuilder();
  let backingStore = builder.addArray(kWasmI32, true);
  let arrayStruct = builder.addStruct([
    makeField(kWasmI32 /*length*/, true),
    makeField(wasmRefType(backingStore), true)
  ]);

  builder.addFunction('createArray',
      makeSig([kWasmI32 /*length*/], [kWasmExternRef]))
    .addLocals(kWasmI32, 1)  // i
    .addLocals(wasmRefType(backingStore), 1) // backingStore
    .addBody([
      // i = length;
      kExprLocalGet, 0, // length
      kExprLocalTee, 1,
      // backingStore = new backingStore[length];
      kGCPrefix, kExprArrayNewDefault, backingStore,
      kExprLocalSet, 2,
      // while (true)
      kExprLoop, kWasmVoid,
        // backingStore[--i] = i;
        kExprLocalGet, 2, // backingStore
        kExprLocalGet, 1, // i
        kExprI32Const, 1,
        kExprI32Sub,
        kExprLocalTee, 1,
        kExprLocalGet, 1, // i
        kGCPrefix, kExprArraySet, backingStore,
        // if (i != 0) continue;
        kExprLocalGet, 1,
        kExprI32Const, 0,
        kExprI32Ne,
        kExprBrIf, 0,
        // break;
      kExprEnd,
      // return new arrayStruct(length, backingStore);
      kExprLocalGet, 0, // length
      kExprLocalGet, 2, // backingStore
      kGCPrefix, kExprStructNew, arrayStruct,
      kGCPrefix, kExprExternExternalize,
    ])
    .exportFunc();

  builder.addFunction('getLength',
      makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCast, arrayStruct,
      kGCPrefix, kExprStructGet, arrayStruct, 0,
    ])
    .exportFunc();

  builder.addFunction('get', makeSig([kWasmExternRef, kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCast, arrayStruct,
      kGCPrefix, kExprStructGet, arrayStruct, 1,
      kExprLocalGet, 1,
      kGCPrefix, kExprArrayGet, backingStore,
    ])
    .exportFunc();

  builder.addFunction('wasmSumArray', makeSig([kWasmExternRef], [kWasmI32]))
    .addLocals(kWasmI32, 2)  // index, result
    .addBody([
      // index = cast<arrayStruct>(internalize(local.get 0)).length;
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCast, arrayStruct,
      kGCPrefix, kExprStructGet, arrayStruct, 0,
      kExprLocalTee, 1,
      // if (index == 0) return 0;
      kExprI32Eqz,
      kExprIf, kWasmVoid,
        kExprI32Const, 0,
        kExprReturn,
      kExprEnd,

      // while (true)
      kExprLoop, kWasmVoid,
        // result =
        //   cast<arrayStruct>(internalize(local.get 0)).backingStore[--index]
        //   + result;
        kExprLocalGet, 0,
        kGCPrefix, kExprExternInternalize,
        kGCPrefix, kExprRefCast, arrayStruct,
        kGCPrefix, kExprStructGet, arrayStruct, 1,

        kExprLocalGet, 1,
        kExprI32Const, 1,
        kExprI32Sub,
        kExprLocalTee, 1,

        kGCPrefix, kExprArrayGet, backingStore,
        kExprLocalGet, 2,  // result
        kExprI32Add,
        kExprLocalSet, 2,
        // if (index != 0) continue;
        kExprLocalGet, 1,
        kExprI32Const, 0,
        kExprI32Ne,
        kExprBrIf, 0,
      kExprEnd,
      // return result;
      kExprLocalGet, 2,
    ])
    .exportFunc();


  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let arrayLength = 10_000;
  let myArrayStruct = wasm.createArray(arrayLength);
  let jsArray = Array.from(Array(arrayLength).keys());

  // expected = 0 + 1 + 2 + ... + (arrayLength - 1)
  let expected = (arrayLength - 1) * arrayLength / 2;

  let benchmarks = [
    ["WasmLoop", (function WasmLoop() {
      assertEquals(expected, wasm.wasmSumArray(myArrayStruct));
    })],
    ["JsLoop", (function JsLoop() {
      let get = wasm.get;
      let length = wasm.getLength(myArrayStruct);
      let result = 0;
      for (let i = 0; i < length; ++i) {
        result += get(myArrayStruct, i);
      }
      assertEquals(expected, result);
    })],
    ["PureJSLoop", function PureJSLoop() {
      let length = jsArray.length;
      let result = 0;
      for (let i = 0; i < length; ++i) {
        result += jsArray[i];
      }
      assertEquals(expected, result);
    }]
  ];

  for (let [name, fct] of benchmarks) {
    new BenchmarkSuite(name, [5], [new Benchmark(name, false, false, 0, fct)]);
  }
})();
