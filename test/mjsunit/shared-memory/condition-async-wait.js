// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

(function TestAsyncWait() {
  let mutex = new Atomics.Mutex;
  let cv = new Atomics.Condition;
  let notified = false;

  let locked = Atomics.Mutex.lockAsync(mutex, async function() {
    await Atomics.Condition.waitAsync(cv, mutex);
    notified = true;
  });

  // The lock is held, but the callback is not executed until the next
  // microtask.
  assertFalse(Atomics.Mutex.tryLock(mutex, function() {}).success);

  setTimeout(() => {
    // The lock callback has been executed and the condition variable
    // queued one waiter, and the lock is released.
    assertEquals(1, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv));
    assertTrue(Atomics.Mutex.tryLock(mutex, function() {}).success);
    Atomics.Condition.notify(cv, 1);
    // The notification task has been posted, but the execution will continue
    // in the next microtask.
    assertFalse(notified);
    setTimeout(() => {
      assertTrue(notified);
    }, 0);
  }, 0);
})();
