// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct

(function TestAsyncPromiseResolveValueAndChain() {
  let mutex = new Atomics.Mutex();

  // Ensure the promise is resolved with the return value of the callback.
  let asyncPromise = Atomics.Mutex.lockAsync(mutex, function() {
    return 2;
  });
  asyncPromise.then((result) => {
    assertEquals(result, 2);
  });

  let sequence = 0;
  let releaseWait;
  // asyncPromise2 will resolve when WaitPromise is resolved in this case.
  let waitPromise = new Promise((resolve) => {
    releaseWait = resolve;
  });
  let asyncPromise2 = Atomics.Mutex.lockAsync(mutex, async function() {
    return waitPromise;
  });

  releaseWait(3);
  // WaitPromise.then should run before asyncPromise2.then
  // Both should resolve to the same value passed through ReleaseWait
  waitPromise.then((result) => {
    assertEquals(sequence, 0);
    sequence++;
    assertEquals(result, 3);
  });
  asyncPromise2.then((result) => {
    assertEquals(result, 3)
    assertEquals(sequence, 1);
  });
})();

(function TestLockAsyncOnLockedMutex() {
  let mutex = new Atomics.Mutex();
  let arr = new SharedArray(5);
  arr[0] = mutex;
  arr[1] = 0;
  let script = `
    onmessage = function(e) {
        let mutex = e.arr[0];
        Atomics.Mutex.lock(mutex, function() {
            postMessage("started");
            for (let i = 0; i < 1000000; i++) {
                let a = 2 * 5 * 3 * 1 * 90 * 4123 * 124 * 1231;
                let b = a / 3 / 2 / 5 / 3 / 2 / 9;
            }
            postMessage("ended");
            e.arr[1] = 1;
        });
    };`;
  let worker = new Worker(script, {type: 'string'});

  worker.postMessage({arr});
  assertEquals('started', worker.getMessage());
  assertEquals(arr[1], 0);
  Atomics.Mutex.lockAsync(mutex, function() {
    assertEquals(arr[1], 1);
  });
  // lockAsync doesn't block the thread, so this should run before lockAsync's
  // callback
  assertEquals(arr[1], 0);
  assertEquals('ended', worker.getMessage());
})();
