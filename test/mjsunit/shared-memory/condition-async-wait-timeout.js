// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct

(function TestAsyncWaitTimeout() {
  let mutex = new Atomics.Mutex;
  let cv = new Atomics.Condition;
  let notified = false;

  Atomics.Mutex.lockAsync(mutex, async function() {
    await Atomics.Condition.waitAsync(cv, mutex, 0);
    notified = true;
  });
  assertEquals(false, notified);
  setTimeout(() => {
    assertEquals(true, notified);
  }, 0);
})();
