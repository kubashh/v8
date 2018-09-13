// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-sharedarraybuffer
// Flags: --experimental-wasm-threads

'use strict';

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

function GetAtomicWakeFunction(memory, offset) {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 0, 20, "shared");
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprGetLocal, 0,
      kExprGetLocal, 1,
      kAtomicPrefix,
      kExprI32AtomicWake, /* alignment */ 0, offset])
    .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  return instance.exports.main;
}

(function TestInvalidIndex() {
  let sab = new SharedArrayBuffer(16);
  let i32a = new Int32Array(sab);

  // Valid indexes are 0-3.
  [-1, 4, 100, 0xffffffff].forEach(function(invalidIndex) {
    assertThrows(function() {
      Atomics.wait(i32a, invalidIndex, 0);
    }, RangeError);
    assertThrows(function() {
      Atomics.wake(i32a, invalidIndex, 0);
    }, RangeError);
    let validIndex = 0;
  });

  i32a = new Int32Array(sab, 8);
  [-1, 2, 100, 0xffffffff].forEach(function(invalidIndex) {
    assertThrows(function() {
      Atomics.wait(i32a, invalidIndex, 0);
    }, RangeError);
    assertThrows(function() {
      Atomics.wake(i32a, invalidIndex, 0);
    }, RangeError);
    let validIndex = 0;
  });
});

(function TestWaitTimeout() {
  let i32a = new Int32Array(new SharedArrayBuffer(16));
  let waitMs = 100;
  let startTime = new Date();
  assertEquals("timed-out", Atomics.wait(i32a, 0, 0, waitMs));
  let endTime = new Date();
  assertTrue(endTime - startTime >= waitMs);
})();

(function TestWaitNotEqual() {
  let sab = new SharedArrayBuffer(16);
  let i32a = new Int32Array(sab);
  assertEquals("not-equal", Atomics.wait(i32a, 0, 42));

  i32a = new Int32Array(sab, 8);
  i32a[0] = 1;
  assertEquals("not-equal", Atomics.wait(i32a, 0, 0));
});

(function TestWaitNegativeTimeout() {
  let i32a = new Int32Array(new SharedArrayBuffer(16));
  assertEquals("timed-out", Atomics.wait(i32a, 0, 0, -1));
  assertEquals("timed-out", Atomics.wait(i32a, 0, 0, -Infinity));
});

(function TestWaitNotAllowed() {
  %SetAllowAtomicsWait(false);
  let i32a = new Int32Array(new SharedArrayBuffer(16));
  assertThrows(function() {
    Atomics.wait(i32a, 0, 0, -1);
  });
  %SetAllowAtomicsWait(true);
});

//// WORKER ONLY TESTS

if (this.Worker) {

  let TestWaitWithTimeout = function(notify, timeout) {
    let memory = new WebAssembly.Memory({initial: 16, maximum: 16, shared: true});
    let sab = memory.buffer;
    let i32a = new Int32Array(sab);
    let offset = 0;

    let workerScript =
      `onmessage = function(msg) {
         let i32a = new Int32Array(msg.sab, msg.offset);
         let result = Atomics.wait(i32a, 0, 0, ${timeout});
         postMessage(result);
       };`;

    let worker = new Worker(workerScript);
    worker.postMessage({sab, offset});

    // Spin until the worker is waiting on the futex.
    while (%AtomicsNumWaitersForTesting(i32a, 0) != 1) {}

    notify(memory, offset, 0, 1);
    assertEquals("ok", worker.getMessage());
    worker.terminate();

    let worker2 = new Worker(workerScript);
    offset = 8;
    let i32a2 = new Int32Array(sab, offset);
    worker2.postMessage({sab, offset});

    // Spin until the worker is waiting on the futex.
    while (%AtomicsNumWaitersForTesting(i32a2, 0) != 1) {}
    notify(memory, offset, 0, 1);
    assertEquals("ok", worker2.getMessage());
    worker2.terminate();

    // Futex should work when index and buffer views are different, but
    // the real address is the same.
    let worker3 = new Worker(workerScript);
    offset = 4;
    i32a2 = new Int32Array(sab, offset);
    worker3.postMessage({sab, offset: 8});

    // Spin until the worker is waiting on the futex.
    while (%AtomicsNumWaitersForTesting(i32a2, 1) != 1) {}
    notify(memory, offset, 4, 1);
    assertEquals("ok", worker3.getMessage());
    worker3.terminate();
  };

  // Test various infinite timeouts
  /*
  TestWaitWithTimeout(Atomics.wake, undefined);
  TestWaitWithTimeout(Atomics.wake, NaN);
  TestWaitWithTimeout(Atomics.wake, Infinity);
  TestWaitWithTimeout(Atomics.notify, undefined);
  TestWaitWithTimeout(Atomics.notify, NaN);
  TestWaitWithTimeout(Atomics.notify, Infinity);
  */
  let adapter = (mem, off, index, count) => {
    return GetAtomicWakeFunction(mem, off)(index, count);
  };
  TestWaitWithTimeout(adapter, undefined);
  TestWaitWithTimeout(adapter, NaN);
  TestWaitWithTimeout(adapter, Infinity);

  let TestWakeMulti = function(notify) {
    let memory = new WebAssembly.Memory({initial: 20, maximum: 20, shared: true});
    let sab = memory.buffer;
    let i32a = new Int32Array(sab);

    // SAB values:
    // i32a[id], where id in range [0, 3]:
    //   0 => Worker |id| is still waiting on the futex
    //   1 => Worker |id| is not waiting on futex, but has not be reaped by the
    //        main thread.
    //   2 => Worker |id| has been reaped.
    //
    // i32a[4]:
    //   always 0. Each worker is waiting on this index.

    let workerScript =
      `onmessage = function(msg) {
         let id = msg.id;
         let i32a = new Int32Array(msg.sab);

         // Wait on i32a[4] (should be zero).
         let result = Atomics.wait(i32a, 4, 0);
         // Set i32a[id] to 1 to notify the main thread which workers were
         // woken up.
         Atomics.store(i32a, id, 1);
         postMessage(result);
       };`;

    let workers = [];
    for (let id = 0; id < 4; id++) {
      workers[id] = new Worker(workerScript);
      workers[id].postMessage({sab, id});
    }

    // Spin until all workers are waiting on the futex.
    while (%AtomicsNumWaitersForTesting(i32a, 4) != 4) {}

    // Wake up three waiters.
    assertEquals(3, notify(memory, 0, 16, 3));

    let wokenCount = 0;
    let waitingId = 0 + 1 + 2 + 3;
    while (wokenCount < 3) {
      for (let id = 0; id < 4; id++) {
        // Look for workers that have not yet been reaped. Set i32a[id] to 2
        // when they've been processed so we don't look at them again.
        if (Atomics.compareExchange(i32a, id, 1, 2) == 1) {
          assertEquals("ok", workers[id].getMessage());
          workers[id].terminate();
          waitingId -= id;
          wokenCount++;
        }
      }
    }

    assertEquals(3, wokenCount);
    assertEquals(0, Atomics.load(i32a, waitingId));
    assertEquals(1, %AtomicsNumWaitersForTesting(i32a, 4));

    // Finally wake the last waiter.
    assertEquals(1, notify(memory, 0, 16, 1));
    assertEquals("ok", workers[waitingId].getMessage());
    workers[waitingId].terminate();

    assertEquals(0, %AtomicsNumWaitersForTesting(i32a, 4));

  };

  let adapter2 = (mem, off, index, count) => {
    return GetAtomicWakeFunction(mem, off)(index, count);
  };
  TestWakeMulti(adapter2);
}
