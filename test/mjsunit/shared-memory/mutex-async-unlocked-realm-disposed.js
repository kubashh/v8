// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

if (this.Worker) {
  (function TestAsyncLockedWorkerTerminated() {
    let workerLockScript = `onmessage = function(msg) {
      let {mutex, sharedObj} = msg;
      Atomics.Mutex.lock(mutex, function() {
        postMessage('Lock acquired');
        while (!Atomics.load(sharedObj, 'done')) {}
      });
      postMessage('Lock released');
    };`;

    let realmLockAsyncScript = `
      Atomics.Mutex.lockAsync(Realm.shared.mutex, async function() {});`;

    let workerLock1 = new Worker(workerLockScript, {type: 'string'});
    let workerLock2 = new Worker(workerLockScript, {type: 'string'});
    let realmLockAsyc = Realm.create();

    let mutex = new Atomics.Mutex;
    let SharedType = new SharedStructType(['done']);
    let sharedObj = new SharedType();
    sharedObj.done = false;
    workerLock1.postMessage({mutex, sharedObj});
    assertEquals('Lock acquired', workerLock1.getMessage());
    Realm.shared = {mutex, assertEquals};
    Realm.eval(realmLockAsyc, realmLockAsyncScript);
    // There is an async waiter from the realm.
    assertEquals(
        1, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex));
    workerLock2.postMessage({mutex, sharedObj});
    let asyncLockPromise = Atomics.Mutex.lockAsync(mutex, async function() {});
    let asyncLockPromiseResolved = false;
    asyncLockPromise.then(() => {
      asyncLockPromiseResolved = true;
    });

    // There is an async waiter from the main thread.
    assertEquals(
        2, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex));
    // There is a sync waiter from the worker.
    while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex) !== 3) {
    }
    Realm.dispose(realmLockAsyc);
    // The async waiter from the realm is terminated.
    assertEquals(
        2, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex));

    sharedObj.done = true;
    assertEquals('Lock released', workerLock1.getMessage());
    let asyncLoop = () => {
      if (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex) !== 0) {
        setTimeout(asyncLoop, 0);
        return;
      }
      assertEquals('Lock acquired', workerLock2.getMessage());
      assertEquals('Lock released', workerLock2.getMessage());
      workerLock1.terminate();
      workerLock2.terminate();
      setTimeout(() => {
        assertEquals(true, asyncLockPromiseResolved);
      }, 0);
    };
    asyncLoop();
  })();
}
