// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class A {
  constructor(arg) {
    return arg;
  }
}

class B extends A {
  #b = 1;  // ACCESS_CHECK -> DATA
  constructor(arg) {
    super(arg);
  }
  static setField(obj) {
    obj.#b = 'b';  // KeyedStoreIC
  }
  static getField(obj) {
    return obj.#b;
  }
}

class C extends A {
  #c;  // DefineKeyedOwnIC: ACCESS_CHECK -> NOT_FOUND
  constructor(arg) {
    super(arg);
  }
  static setField(obj) {
    obj.#c = 'c';  // KeyedStoreIC
  }
  static getField(obj) {
    return obj.#c;
  }
}

const realm = Realm.createAllowCrossRealmAccess();
const g = Realm.global(realm);
Realm.detachGlobal(realm);

assertThrows(() => new B(g), Error, /no access/);
assertThrows(() => new C(g), Error, /no access/);
assertThrows(() => B.setField(g), Error, /no access/);
assertThrows(() => C.setField(g), Error, /no access/);
assertThrows(() => B.getField(g), Error, /no access/);
assertThrows(() => C.getField(g), Error, /no access/);

const realm2 = Realm.createAllowCrossRealmAccess();
const g2 = Realm.global(realm2);

assertThrows(() => B.setField(g2), TypeError, /Cannot write private member #b to an object whose class did not declare it/);
assertThrows(() => B.getField(g2), TypeError, /Cannot read private member #b from an object whose class did not declare it/);

new B(g2);
assertEquals(B.getField(g2), 1);
B.setField(g2);
assertEquals(B.getField(g2), 'b');  // Fast case
B.setField(g2);  // Fast case
assertEquals(B.getField(g2), 'b');  // Fast case
assertThrows(() => new B(g2), TypeError, /Cannot initialize #b twice on the same object/);

assertThrows(() => C.setField(g2), TypeError, /Cannot write private member #c to an object whose class did not declare it/);
assertThrows(() => C.getField(g2), TypeError, /Cannot read private member #c from an object whose class did not declare it/);

new C(g2);
assertEquals(C.getField(g2), undefined);
C.setField(g2);
assertEquals(C.getField(g2), 'c');  // Fast case
C.setField(g2);  // Fast case
assertEquals(C.getField(g2), 'c');  // Fast case
assertThrows(() => new C(g2), TypeError, /Cannot initialize #c twice on the same object/);

Realm.detachGlobal(realm2);

assertThrows(() => B.setField(g2), Error, /no access/);
assertThrows(() => C.setField(g2), Error, /no access/);
assertThrows(() => B.getField(g), Error, /no access/);
assertThrows(() => C.getField(g), Error, /no access/);
assertThrows(() => new B(g2), Error, /no access/);
assertThrows(() => new C(g2), Error, /no access/);
