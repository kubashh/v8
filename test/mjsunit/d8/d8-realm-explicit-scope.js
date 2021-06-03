// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function callString(f) {
  return '(' + f.toString() + ')()';
}

(function TestExplicitRealmScope() {
  function createObjects() {
    globalThis.foo = 42;
  }
  const r1 = Realm.create();
  Realm.eval(r1, callString(createObjects));
  function useObjects() {
    return globalThis.foo;
  }
  const foo1 = Realm.eval(r1, callString(useObjects));
  assertEquals(42, foo1);
  const r2 = Realm.create();
  const foo2 = Realm.eval(r2, callString(useObjects));
  assertEquals(undefined, foo2);
})();
