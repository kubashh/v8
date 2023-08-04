// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct

let mutex = new Atomics.Mutex();

// Ensure the promise is resolved with the return value of the callback.
let asyncPromise = Atomics.Mutex.lockAsync(mutex,function() {
    return 2;
});
asyncPromise.then((result) => {
    assertEquals(result, 2);
}
);

let sequence = 0;
let ReleaseWait;
let WaitPromise = new Promise((resolve) => { ReleaseWait = resolve});
let asyncPromise2  = Atomics.Mutex.lockAsync(mutex,async function() {
    return WaitPromise;
});

ReleaseWait(3);
// WaitPromise.then should run before asyncPromise2.then
// Both should resolve to the same value passed through ReleaseWait
WaitPromise.then((result) => {
    assertEquals(sequence, 0);
    sequence++;
    assertEquals(result, 3);
});
asyncPromise2.then((result) => {
    assertEquals(result, 3)
    assertEquals(sequence, 1);
});
