// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-private-brand-checks

(function TestBasics() {
  class A {
    constructor(x) {
      this.#x = x;
    }
    m(other) {
      if (#x in other) {
        return other.#x;
      }
      return 'not found';
    }
    #x = 1;
  }
  let a1 = new A(3);
  assertEquals(3, a1.m(a1));
  assertEquals(4, a1.m(new A(4)));
  assertEquals('not found', a1.m({}));
  assertEquals('not found', a1.m(function() {}));
  assertThrows(() => { a1.m(100); }, TypeError);
  assertThrows(() => { a1.m('foo'); }, TypeError);
  assertThrows(() => { a1.m(undefined); }, TypeError);
  assertThrows(() => { a1.m(null); }, TypeError);

  class B {
    #x = 5;
  }
  assertEquals('not found', a1.m(new B()));

})();

(function TestPrivateMethodIn() {
  class C {
    #pm() {
    }
    m(other) {
      /*try {
        other.#pm();
        return true;
      } catch {
        return false;
      }*/
      return #pm in other;
    }
  }
  let c1 = new C();
  /*assertEquals(true, c1.m(c1));
  assertEquals(true, c1.m(new C()));
  assertEquals(false, c1.m({}));
  assertThrows(() => { c1.m(100); }, TypeError);
  assertThrows(() => { c1.m('foo'); }, TypeError);
  assertThrows(() => { c1.m(undefined); }, TypeError);
  assertThrows(() => { c1.m(null); }, TypeError);*/

  class B {
    #pm() {}
  }
  assertEquals(false, c1.m(new B()));
})();

(function TestPrivateIdentifiersAreDistinct() {
  function GenerateClass() {
    class C {
      m(other) {
        return #x in other;
      }
      #x = 0;
    }
    return new C();
  }
  let c1 = GenerateClass();
  let c2 = GenerateClass();
  assertEquals(true, c1.m(c1));
  assertEquals(false, c1.m(c2));
  assertEquals(false, c2.m(c1));
  assertEquals(true, c2.m(c2));
})();

// FIXME: inside eval?
// FIXME: private static?
// FIXME: accessor (similar to method)
// FIXME: does it work in subclasses?
// FIXME: what happens for setter only?

// FIXME: tests for combining with other expressions?
// FIXME: test262?

// #foo in {} in {} in {}
// #foo in {} < a + 1
// #foo in {} instanceof 55

// Invalid: #foo in {} = 4 or (#foo in {}) = 4

// FIXME: also for methods
// FIXME: how do accessors work
// FIXME: getter only, setter only, getter and setter

// FIXME: #bar in obj where #bar is not in the enclosing class

// FIXME: check error message positions

// FIXME: static private methods
// FIXME: static private methods for the scope->class_variable() == nullptr case, when does that happen?
