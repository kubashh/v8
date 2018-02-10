// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function() {
  let resolve;
  let onFulfilledCalled = false;
  const p = new Promise(r => resolve = r);
  resolve(Promise.resolve(1));
  p.then(
      v => {
        onFulfilledCalled = true;
        assertEquals(v, 1);
      },
      e => {
        assertUnreachable();
      });
  setTimeout(_ => assertTrue(onFulfilledCalled));
})();

(function() {
  let resolve;
  let onRejectedCalled = false;
  const p = new Promise(r => resolve = r);
  resolve(Promise.reject(1));
  p.then(
      v => {
        assertUnreachable();
      },
      e => {
        onRejectedCalled = true;
        assertEquals(e, 1);
      });
  setTimeout(_ => assertTrue(onRejectedCalled));
})();

(function() {
  let onFulfilledCalled = false;
  (async () => Promise.resolve(1))().then(
      v => {
        onFulfilledCalled = true;
        assertEquals(v, 1);
      },
      e => {
        assertUnreachable();
      });
  setTimeout(_ => assertTrue(onFulfilledCalled));
})();


(function() {
  let onRejectedCalled = false;
  (async () => Promise.reject(1))().then(
      v => {
        assertUnreachable();
      },
      e => {
        onRejectedCalled = true;
        assertEquals(e, 1);
      });
  setTimeout(_ => assertTrue(onRejectedCalled));
})();
