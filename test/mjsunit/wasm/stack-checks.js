// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-type-reflection --experimental-wasm-gc

load("test/mjsunit/wasm/wasm-module-builder.js");

let fast = arguments.length > 0;
print("fast: " + fast);

var instance = (function () {
  var builder = new WasmModuleBuilder();
  // void loop1() {
  //   var i = 0;
  // loop:
  //   i += 1;
  //   if (i < 1,000,000,000) goto loop;
  //   return i;
  // }
  /*builder.addFunction("loop1", kSig_l_v)
    .addLocals(kWasmI64, 1)
    .addBody([kExprLoop, kWasmStmt,
              kExprLocalGet, 0, kExprI64Const, 1, kExprI64Add, kExprLocalTee, 0,
              ...wasmI64Const(1000000000), kExprI64LtU,
              kExprBrIf, 0, kExprEnd,
              kExprLocalGet, 0])
    .exportFunc();
 */
  builder.addFunction("loop1", kSig_l_v)
    .addLocals(kWasmI64, 1)
    .addBody([kExprBlock, kWasmStmt,
                kExprLoop, kWasmI64,
                  kExprLocalGet, 0, kExprI64Const, 1, kExprI64Add, kExprLocalSet, 0,
                  kExprLocalGet, 0, ...wasmI64Const(1000000000), kExprI64GeU, kExprBrIf, 1,
                  kExprLocalGet, 0, ...wasmI64Const(1000000000), kExprI64LtU, kExprBrIf, 0,
                  kExprI64Const, 55,
                kExprEnd,
                kExprLocalGet, 0, kExprI64Add, kExprLocalSet, 0,
              kExprEnd,
              kExprLocalGet, 0, kExprI64Const, 42, kExprI64Sub])
    .exportFunc();

/*
    builder.addFunction("loop1", kSig_l_v)
    .addLocals(kWasmI64, 1)
    .addBody([kExprLoop, kWasmI64,
                kExprLocalGet, 0, kExprI64Const, 1, kExprI64Add, kExprLocalSet, 0,
                kExprLocalGet, 0, ...wasmI64Const(50), kExprI64GeU,
                kExprIf, kWasmI64,
                  kExprLocalGet, 0, ...wasmI64Const(100), kExprI64LtU,
                  kExprBrIf, 1,
                  kExprI64Const, 42,
                kExprElse,
                  kExprI64Const, 54,
                kExprEnd,
              kExprEnd,
              kExprLocalGet, 0, kExprI64Sub])
    .exportFunc();
*/
/*
builder.addFunction("loop1", kSig_l_v)
.addLocals(kWasmI64, 1)
.addBody([kExprBlock, kWasmStmt,
          kExprLoop, kWasmStmt,
          kExprI64Const, 11, kExprLocalSet, 0,
          kExprI32Const, 0, kExprBrIf, 1,
          kExprI32Const, 1, kExprBrIf, 0,
          kExprEnd,
          kExprLocalGet, 0, kExprI64Const, 22, kExprI64Add, kExprLocalSet, 0,
          kExprEnd,
          kExprLocalGet, 0, kExprI64Const, 33, kExprI64Sub])
.exportFunc();
*/

/*
  // void loop2() {
  //   var i = 0;
  //   var j = 0;
  // loop:
  //   i += 1;
  //   j += 1;
  //   if (i+j < 2,000,000,000) goto loop;
  //   return i;
  // }
  builder.addFunction("loop2", kSig_l_v)
    .addLocals(kWasmI64, 2)
    .addBody([kExprLoop, kWasmStmt,
              kExprLocalGet, 0, kExprI64Const, 1, kExprI64Add, kExprLocalTee, 0,
              kExprLocalGet, 1, kExprI64Const, 1, kExprI64Add, kExprLocalTee, 1,
              kExprI64Add, ...wasmI64Const(2000000000), kExprI64LtU,
              kExprBrIf, 0, kExprEnd,
              kExprLocalGet, 0])
    .exportFunc();

  // void loop3() {
  //   var i = 0;
  // loop:
  //   i = i + 2 - 1;
  //   if (i < 1,000,000,000) goto loop;
  //   return i;
  // }
  builder.addFunction("loop3", kSig_l_v)
    .addLocals(kWasmI64, 1)
    .addBody([kExprLoop, kWasmStmt,
              kExprLocalGet, 0, kExprI64Const, 2, kExprI64Add,
              kExprI64Const, 1, kExprI64Sub, kExprLocalTee, 0,
              ...wasmI64Const(1000000000), kExprI64LtU,
              kExprBrIf, 0, kExprEnd,
              kExprLocalGet, 0])
    .exportFunc();
*/
  return builder.instantiate({});
})();
assertTrue(!!instance);

{
  var time_before = performance.now();
  print (instance.exports.loop1());
  var time_after = performance.now();
  var total_time = time_after - time_before;
  print("loop1: " + total_time);
}
/*{
  var time_before = performance.now();
  instance.exports.loop2();
  var time_after = performance.now();
  var total_time = time_after - time_before;
  print("loop2: " + total_time);
}
{
  var time_before = performance.now();
  instance.exports.loop3();
  var time_after = performance.now();
  var total_time = time_after - time_before;
  print("loop3: " + total_time);
}*/
