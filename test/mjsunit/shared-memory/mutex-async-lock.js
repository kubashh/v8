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
  let resolverExecuted = false;
  lockPromise.then((result) => {
    // Ensure the promise's result value property is set to the return
    // value of the callback.
    assertEquals(42, result.value);
    assertEquals(true, result.success);
    resolverExecuted = true;
  });
  setTimeout(() => {
    assertEquals(resolverExecuted, true);
  }, 0);
})();
