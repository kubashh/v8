// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

if (this.Worker) {
  (function TestAsyncLockedWorkerTerminated() {
    let workerLockScript = `onmessage = function(msg) {
      let {mutex} = msg;
      Atomics.Mutex.lock(mutex, function() {
        postMessage('Lock acquired');
      });
      postMessage('Lock released');
    };`;

    let realmLockAsyncScript = `
      let unresolvedPromise = new Promise(() => {});
      Atomics.Mutex.lockAsync(Realm.shared.mutex, async function() {
        Realm.shared.realmLocked = true;
        await unresolvedPromise;
      });`;

    let workerLock = new Worker(workerLockScript, {type: 'string'});
    let realmLockAsyc = Realm.create();

    let mutex = new Atomics.Mutex;

    Realm.shared = {mutex, realmLocked: false};
    Realm.eval(realmLockAsyc, realmLockAsyncScript);

    // Flush the microtask queue.
    setTimeout(() => {
      assertEquals(true, Realm.shared.realmLocked);
      workerLock.postMessage({mutex});
      // There is one waiter from the worker.
      while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex) !==
             1) {
      }
      Realm.dispose(realmLockAsyc);
      assertEquals('Lock acquired', workerLock.getMessage());
      assertEquals('Lock released', workerLock.getMessage());
      workerLock.terminate();
    }, 0);
  })();
}
