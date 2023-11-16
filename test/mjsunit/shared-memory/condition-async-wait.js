// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct

(function TestAsyncWait() {
  let mutex = new Atomics.Mutex;
  let cv = new Atomics.Condition;
  let notified = false;

  let locked = Atomics.Mutex.lockAsync(mutex, async function() {
    await Atomics.Condition.waitAsync(cv, mutex);
    console.log('executing');
    notified = true;
  });

  setTimeout(() => {
    Atomics.Condition.notify(cv, 1);
    assertEquals(false, notified);
    setTimeout(() => {
      assertEquals(true, notified);
    }, 0);
  }, 0);
})();
