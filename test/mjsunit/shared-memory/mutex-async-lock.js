// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct

(function TestAsyncLock() {
  let mutex = new Atomics.Mutex;

  let lockPromise = Atomics.Mutex.lockAsync(mutex, function() {
    return 42;
  })
  let chain_executed = false;
  lockPromise.then((result) => {
    // Ensure the promise's result value property is set to the return
    // value of the callback.
    assertEquals(42, result.value);
    assertEquals(true, result.success);
    chain_executed = true;
  });
  setTimeout(() => {
    assertEquals(chain_executed, true);
  }, 0);
})();
