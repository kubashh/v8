// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

if (this.Worker) {
  (function TestWorkerTerminated() {
    let workerLockScript = `onmessage = function(msg) {
      let {mutex, sharedObj} = msg;
      Atomics.Mutex.lock(mutex, function() {
        postMessage('Lock acquired');
        while(!Atomics.load(sharedObj, 'done')) {}
      });
      postMessage('Lock released');
    };`;

    let workerWaitScript = `onmessage = function(msg) {
      let {cv_mutex, cv, shared_Obj} = msg;
      Atomics.Mutex.lock(cv_mutex, function() {
        postMessage('Waiting started');
        Atomics.Condition.wait(cv, cv_mutex);
      });
      postMessage('Waiting done');
    };`;

    let workerAsyncScript = `onmessage = function(msg) {
      if (msg.type === 'lock') {
        let {mutex, sharedObj} = msg;
        for (let i = 0; i < 10; i++) {
          Atomics.Mutex.lockAsync(mutex, async function() {})
        }
        postMessage('Lock waiters queued');
      }
      else if (msg.type === 'wait'){
        let {cv_mutex, cv} = msg;
        let count = 0;
        for (let i = 0; i < 10; i++) {
            Atomics.Mutex.lockAsync(cv_mutex, async function() {
              count++;
              await Atomics.Condition.waitAsync(cv, cv_mutex);
            })
          }
        let asyncLoop = () => {
          if (count !== 10) {
            setTimeout(asyncLoop, 0);
            return;
          }
          postMessage('Wait waiters queued');
        }
        asyncLoop();
      }
      else {
        postMessage(%AtomicsSychronizationNumAsyncUnlockedWaitersInIsolate());
      }
    };`;

    let workerLock1 = new Worker(workerLockScript, {type: 'string'});
    let workerLock2 = new Worker(workerLockScript, {type: 'string'});
    let workerWait1 = new Worker(workerWaitScript, {type: 'string'});
    let workerWait2 = new Worker(workerWaitScript, {type: 'string'});
    let workerAsync = new Worker(workerAsyncScript, {type: 'string'});

    let SharedType = new SharedStructType(['done']);
    let sharedObj = new SharedType();
    sharedObj.done = false;

    let mutex = new Atomics.Mutex;
    let cv_mutex = new Atomics.Mutex;
    let cv = new Atomics.Condition;
    let lock_msg = {mutex, sharedObj, type: 'lock'};
    let wait_msg = {cv_mutex, cv, type: 'wait'};
    let count_msg = {type: 'count'};
    workerLock1.postMessage(lock_msg);
    workerWait1.postMessage(wait_msg);
    assertEquals('Lock acquired', workerLock1.getMessage());
    assertEquals('Waiting started', workerWait1.getMessage());
    workerAsync.postMessage(lock_msg);
    workerAsync.postMessage(wait_msg);
    assertEquals('Lock waiters queued', workerAsync.getMessage());
    assertEquals('Wait waiters queued', workerAsync.getMessage());
    // There are 20 waiters in the async unlocked waiter queue for the
    // workerAsync's isolate.
    workerAsync.postMessage(count_msg);
    assertEquals(20, workerAsync.getMessage());
    workerLock2.postMessage(lock_msg);
    workerWait2.postMessage(wait_msg);
    assertEquals('Waiting started', workerWait2.getMessage());
    while(%AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv) !== 12) {
    }
    workerAsync.terminate();
    sharedObj.done = true;
    Atomics.Condition.notify(cv, 20);
    assertEquals('Lock released', workerLock1.getMessage());
    assertEquals('Lock acquired', workerLock2.getMessage());
    assertEquals('Lock released', workerLock2.getMessage());
    assertEquals('Waiting done', workerWait1.getMessage());
    assertEquals('Waiting done', workerWait2.getMessage());
    workerLock1.terminate();
    workerLock2.terminate();
    workerWait1.terminate();
    workerWait2.terminate();
  })();
}
