// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct

(async function TestAsyncWait() {
  let mutex = new Atomics.Mutex;
  let cv = new Atomics.Condition;
  let notified = false;

  Atomics.Mutex.lockAsync(mutex, async function() {
    await Atomics.Condition.waitAsync(cv, mutex);
    notified = true;
  });

  assertEquals(false, notified);
  Atomics.Condition.notify(cv);
  setTimeout(() => {
    assertEquals(true, notified);
  }, 1000);
})();

(async function TestAsyncWaitTimeout() {
  let mutex = new Atomics.Mutex;
  let cv = new Atomics.Condition;
  let notified = false;

  Atomics.Mutex.lockAsync(mutex, async function() {
    await Atomics.Condition.waitAsync(cv, mutex, 1);
    notified = true;
    console.log('executing');
  });

  assertEquals(false, notified);
  setTimeout(() => {
    assertEquals(true, notified);
  }, 1000);
})();
