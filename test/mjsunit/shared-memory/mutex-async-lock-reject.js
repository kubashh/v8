// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct

(function TestAsyncLock() {
  let mutex = new Atomics.Mutex;

  let lockPromise = Atomics.Mutex.lockAsync(mutex, function() {
    throw new Error('Callback thrown');
  })
  let rejectExecuted = false;
  lockPromise.then(
      (result) => {
        assertUnreachable();
      },
      (error) => {
        rejectExecuted = true;
        assertEquals('Callback thrown', error.message);
      });
  setTimeout(() => {
    assertEquals(true, rejectExecuted);
  }, 0);
})();
