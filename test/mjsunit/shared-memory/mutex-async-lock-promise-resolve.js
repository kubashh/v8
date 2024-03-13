// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct

(function TestAsyncPromiseResolveValueAndChain() {
  let mutex = new Atomics.Mutex();

  let thenExecuted = false;
  let waitResolve;
  // asyncPromise2 will resolve when WaitPromise is resolved in this case.
  let waitPromise = new Promise((resolve) => {
    waitResolve = resolve;
  });
  let lockPromise = Atomics.Mutex.lockAsync(mutex, async function() {
    return waitPromise;
  });

  waitResolve(42);

  lockPromise.then((result) => {
    thenExecuted = true;
    assertEquals(42, result.value);
    assertEquals(true, result.success);
  });

  let asyncLoop = () => {
    if (!thenExecuted) {
      setTimeout(asyncLoop, 0);
    }
  };

  asyncLoop();
})();
