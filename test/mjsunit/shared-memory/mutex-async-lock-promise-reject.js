// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

(function TestAsyncLock() {
  let mutex = new Atomics.Mutex;

  let lockPromise = Atomics.Mutex.lockAsync(mutex, function() {
    throw new Error('Callback thrown');
  })
  let rejectExecuted = false;
  let lockWasReleased = false;
  let numLockedWaitersInHandler = 0;
  lockPromise.then(
      (result) => {
        assertUnreachable();
      },
      (error) => {
        lockWasReleased = Atomics.Mutex.tryLock(mutex, function() {}).success;
        numLockedWaitersInHandler =
            %AtomicsSychronizationNumAsyncLockedWaitersInIsolate();
        rejectExecuted = true;
        assertEquals('Callback thrown', error.message);
      });
  // The lock and then callbacks will be executed when the microtask queue
  // is flushed, before proceeding to the next task.
  setTimeout(() => {
    assertTrue(rejectExecuted);
    assertTrue(rejectExecuted);
    assertEquals(0, numLockedWaitersInHandler);
  }, 0);
})();
