// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-sharedarraybuffer --harmony-atomics-waitasync --expose-gc

(function test() {
  let sab = new SharedArrayBuffer(16);
  let i32a = new Int32Array(sab);

  let resolved = false;
  (function() {
    let result = Atomics.waitAsync(i32a, 0, 0);
    result.value.then(
      (value) => { assertEquals("ok", value); resolved = true; },
      () => { assertUnreachable(); });
    })();
  // Make sure result gets gc()d.
  gc();

  let notify_return_value = Atomics.notify(i32a, 0, 1);
  assertEquals(1, notify_return_value);
  assertEquals(0, %AtomicsNumWaitersForTesting(i32a, 0));
  assertEquals(1, %AtomicsNumUnresolvedAsyncPromisesForTesting(i32a, 0));

  setTimeout(()=> {
    assertTrue(resolved);
    assertEquals(0, %AtomicsNumUnresolvedAsyncPromisesForTesting(i32a, 0));
  }, 0);
})();
