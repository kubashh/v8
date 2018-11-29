// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-sharedarraybuffer
// Flags: --experimental-wasm-threads

'use strict';

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

function WasmAtomicWakeFunction(memory, offset, index, num) {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 0, 20, "shared");
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprGetLocal, 0,
      kExprGetLocal, 1,
      kAtomicPrefix,
      kExprAtomicWake, /* alignment */ 0, offset])
    .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  return instance.exports.main(index, num);
}

function WasmI32AtomicWaitFunction(memory, offset, index, val, timeout) {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 0, 20, "shared");
  builder.addFunction("main",
    makeSig([kWasmI32, kWasmI32, kWasmF64], [kWasmI32]))
    .addBody([
      kExprGetLocal, 0,
      kExprGetLocal, 1,
      kExprGetLocal, 2,
      kExprI64SConvertF64,
      kAtomicPrefix,
      kExprI32AtomicWait, /* alignment */ 0, offset])
      .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  return instance.exports.main(index, val, timeout);
}

function WasmI64AtomicWaitFunction(memory, offset, index, val_low,
                                   val_high, timeout) {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 0, 20, "shared");
  // Wrapper for I64AtomicWait that takes two I32 values and combines to into
  // I64 for the instruction parameter.
  builder.addFunction("main",
    makeSig([kWasmI32, kWasmI32, kWasmI32, kWasmF64], [kWasmI32]))
    .addLocals({i64_count: 1}) // local that is passed as value param to wait
    .addBody([
      kExprGetLocal, 1,
      kExprI64UConvertI32,
      kExprI64Const, 32,
      kExprI64Shl,
      kExprGetLocal, 2,
      kExprI64UConvertI32,
      kExprI64Ior,
      kExprSetLocal, 4, // Store the created I64 value in local
      kExprGetLocal, 0,
      kExprGetLocal, 4,
      kExprGetLocal, 3,
      kExprI64SConvertF64,
      kAtomicPrefix,
      kExprI64AtomicWait, /* alignment */ 0, offset])
      .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  return instance.exports.main(index, val_high, val_low, timeout);
}

(function TestInvalidIndex() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});

  // Valid indexes are 0-65535 (1 page).
  [-2, 65536, 0xffffffff].forEach(function(invalidIndex) {
    assertThrows(function() {
      WasmAtomicWakeFunction(memory, 0, invalidIndex, -1);
    }, Error);
    assertThrows(function() {
      WasmI32AtomicWaitFunction(memory, 0, invalidIndex, 0, -1);
    }, Error);
    assertThrows(function() {
      WasmI64AtomicWaitFunction(memory, 0, invalidIndex, 0, 0, -1);
    }, Error);
    assertThrows(function() {
      WasmAtomicWakeFunction(memory, invalidIndex, 0, -1);
    }, Error);
    assertThrows(function() {
      WasmI32AtomicWaitFunction(memory, invalidIndex, 0, 0, -1);
    }, Error);
    assertThrows(function() {
      WasmI64AtomicWaitFunction(memory, invalidIndex, 0, 0, 0, -1);
    }, Error);
    assertThrows(function() {
      WasmAtomicWakeFunction(memory, invalidIndex/2, invalidIndex/2, -1);
    }, Error);
    assertThrows(function() {
      WasmI32AtomicWaitFunction(memory, invalidIndex/2, invalidIndex/2, 0, -1);
    }, Error);
    assertThrows(function() {
      WasmI64AtomicWaitFunction(memory, invalidIndex/2, invalidIndex/2, 0, 0, -1);
    }, Error);
  });
})();

(function TestI32WaitTimeout() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});
  var waitMs = 100;
  var startTime = new Date();
  assertEquals(2, WasmI32AtomicWaitFunction(memory, 0, 0, 0, waitMs*1000000));
  var endTime = new Date();
  assertTrue(endTime - startTime >= waitMs);
})();

(function TestI64WaitTimeout() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});
  var waitMs = 100;
  var startTime = new Date();
  assertEquals(2, WasmI64AtomicWaitFunction(memory, 0, 0, 0, 0, waitMs*1000000));
  var endTime = new Date();
  assertTrue(endTime - startTime >= waitMs);
})();

(function TestI32WaitNotEqual() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});
  assertEquals(1, WasmI32AtomicWaitFunction(memory, 0, 0, 42, -1));

  assertEquals(2, WasmI32AtomicWaitFunction(memory, 0, 0, 0, 0));

  let i32a = new Int32Array(memory.buffer);
  i32a[0] = 1;
  assertEquals(1, WasmI32AtomicWaitFunction(memory, 0, 0, 0, -1));
  assertEquals(2, WasmI32AtomicWaitFunction(memory, 0, 0, 1, 0));
})();

(function TestI64WaitNotEqual() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});
  assertEquals(1, WasmI64AtomicWaitFunction(memory, 0, 0, 42, 0, -1));

  assertEquals(2, WasmI64AtomicWaitFunction(memory, 0, 0, 0, 0, 0));

  let i32a = new Int32Array(memory.buffer);
  i32a[0] = 1;
  i32a[1] = 2;
  assertEquals(1, WasmI64AtomicWaitFunction(memory, 0, 0, 0, 0, -1));
  assertEquals(2, WasmI64AtomicWaitFunction(memory, 0, 0, 1, 2, 0));
})();

(function TestWakeCounts() {
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});

  [-1, 0, 4, 100, 0xffffffff].forEach(function(count) {
    WasmAtomicWakeFunction(memory, 0, 0, count);
  });
})();

//// WORKER ONLY TESTS

if (this.Worker) {

  let memory = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});

  let workerScript = `onmessage = function(msg) {
    load("test/mjsunit/wasm/wasm-constants.js");
    load("test/mjsunit/wasm/wasm-module-builder.js");
    ${WasmI32AtomicWaitFunction.toString()}
    ${WasmI64AtomicWaitFunction.toString()}
    let id = msg.id;
    let memory = msg.memory;
    let i32a = new Int32Array(memory.buffer);
    let result = Atomics.wait(i32a, 0>>>2, 0);                    // for wasm-wake 4 threads
    postMessage(result);
    result = Atomics.wait(i32a, 8>>>2, 0);                        // for wasm-wake 5 threads
    postMessage(result);
    result = Atomics.wait(i32a, 16>>>2, 0);                       // for wasm-wake 3 threads
    postMessage(result);
    result = WasmI32AtomicWaitFunction(memory, 0, 24, 0, -1);     // for js-wake 4 threads
    postMessage(result);
    result = WasmI32AtomicWaitFunction(memory, 0, 32, 0, -1);     // for js-wake 5 threads
    postMessage(result);
    result = WasmI32AtomicWaitFunction(memory, 0, 40, 0, -1);     // for js-wake 3 threads
    postMessage(result);
    result = WasmI32AtomicWaitFunction(memory, 0, 48, 0, -1);     // for wasm-wake 4 threads
    postMessage(result);
    result = WasmI32AtomicWaitFunction(memory, 0, 56, 0, -1);     // for wasm-wake 5 threads
    postMessage(result);
    result = WasmI32AtomicWaitFunction(memory, 0, 64, 0, -1);     // for wasm-wake 3 threads
    postMessage(result);
    result = WasmI64AtomicWaitFunction(memory, 0, 72, 0, 0, -1);  // for js-wake 4 threads
    postMessage(result);
    result = WasmI64AtomicWaitFunction(memory, 0, 80, 0, 0, -1);  // for js-wake 5 threads
    postMessage(result);
    result = WasmI64AtomicWaitFunction(memory, 0, 88, 0, 0, -1);  // for js-wake 3 threads
    postMessage(result);
    result = WasmI64AtomicWaitFunction(memory, 0, 96, 0, 0, -1);  // for wasm-wake 4 threads
    postMessage(result);
    result = WasmI64AtomicWaitFunction(memory, 0, 104, 0, 0, -1); // for wasm-wake 5 threads
    postMessage(result);
    result = WasmI64AtomicWaitFunction(memory, 0, 112, 0, 0, -1); // for wasm-wake 3 threads
    postMessage(result);
  };`;

  let js_wake_check = function(memory, index, num, workers, msg) {
    let i32a = new Int32Array(memory.buffer);
    while (%AtomicsNumWaitersForTesting(i32a, index>>>2) != 4) {}
    if (num >= 4) {
      assertEquals(4, Atomics.wake(i32a, index>>>2, num));
    } else {
      assertEquals(num, Atomics.wake(i32a, index>>>2, num));
      assertEquals(4-num, Atomics.wake(i32a, index>>>2, 4));
    }
    for (let id = 0; id < 4; id++) {
      assertEquals(msg, workers[id].getMessage());
    }
  };

  let wasm_wake_check = function(memory, index, num, workers, msg) {
    let i32a = new Int32Array(memory.buffer);
    while (%AtomicsNumWaitersForTesting(i32a, index>>>2) != 4) {}
    if (num >= 4) {
      assertEquals(4, WasmAtomicWakeFunction(memory, 0, index, num));
    } else {
      assertEquals(num, WasmAtomicWakeFunction(memory, 0, index, num));
      assertEquals(4-num, WasmAtomicWakeFunction(memory, 0, index, num));
    }
    for (let id = 0; id < 4; id++) {
      assertEquals(msg, workers[id].getMessage());
    }
  };

  let workers = [];
  for (let id = 0; id < 4; id++) {
    workers[id] = new Worker(workerScript, {type: 'string'});
    workers[id].postMessage({id, memory});
  }

  wasm_wake_check(memory, 0, 4, workers, "ok");
  wasm_wake_check(memory, 8, 5, workers, "ok");
  wasm_wake_check(memory, 16, 3, workers, "ok");

  js_wake_check(memory, 24, 4, workers, 0);
  js_wake_check(memory, 32, 5, workers, 0);
  js_wake_check(memory, 40, 3, workers, 0);

  wasm_wake_check(memory, 48, 4, workers, 0);
  wasm_wake_check(memory, 56, 5, workers, 0);
  wasm_wake_check(memory, 64, 3, workers, 0);

  js_wake_check(memory, 72, 4, workers, 0);
  js_wake_check(memory, 80, 5, workers, 0);
  js_wake_check(memory, 88, 3, workers, 0);

  wasm_wake_check(memory, 96, 4, workers, 0);
  wasm_wake_check(memory, 104, 5, workers, 0);
  wasm_wake_check(memory, 112, 3, workers, 0);

  for (let id = 0; id < 4; id++) {
    workers[id].terminate();
  }
}
