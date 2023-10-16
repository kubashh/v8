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

  assertEquals(notified, false);
  Atomics.Condition.notify(cv);
  setTimeout(() => {assertEquals(notified, true);}, 1000);
})();
