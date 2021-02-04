// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-private-brand-checks

(function TestBasics() {
  class A {
    m(other) {
      return #x in other;
    }
    #x = 1;
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  assertFalse(a.m({}));
  assertFalse(a.m(function() {}));
  assertThrows(() => { a.m(100); }, TypeError);
  assertThrows(() => { a.m('foo'); }, TypeError);
  assertThrows(() => { a.m(undefined); }, TypeError);
  assertThrows(() => { a.m(null); }, TypeError);

  class B {
    #x = 5;
  }
  assertFalse(a.m(new B()));

})();

(function TestPrivateMethod() {
  class A {
    #pm() {
    }
    m(other) {
      return #pm in other;
    }
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  assertFalse(a.m({}));
  assertThrows(() => { a.m(100); }, TypeError);
  assertThrows(() => { a.m('foo'); }, TypeError);
  assertThrows(() => { a.m(undefined); }, TypeError);
  assertThrows(() => { a.m(null); }, TypeError);

  class B {
    #pm() {}
  }
  assertFalse(a.m(new B()));
})();

(function TestPrivateGetter() {
  class A {
    get #foo() {
    }
    m(other) {
      return #foo in other;
    }
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  assertFalse(a.m({}));
  assertThrows(() => { a.m(100); }, TypeError);
  assertThrows(() => { a.m('foo'); }, TypeError);
  assertThrows(() => { a.m(undefined); }, TypeError);
  assertThrows(() => { a.m(null); }, TypeError);

  class B {
    get #foo() {}
  }
  assertFalse(a.m(new B()));
})();

(function TestPrivateSetter() {
  class A {
    set #foo(a) {
    }
    m(other) {
      return #foo in other;
    }
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  assertFalse(a.m({}));
  assertThrows(() => { a.m(100); }, TypeError);
  assertThrows(() => { a.m('foo'); }, TypeError);
  assertThrows(() => { a.m(undefined); }, TypeError);
  assertThrows(() => { a.m(null); }, TypeError);

  class B {
    set #foo(a) {}
  }
  assertFalse(a.m(new B()));
})();

(function TestPrivateGetterAndSetter() {
  class A {
    get #foo() {}
    set #foo(a) {
    }
    m(other) {
      return #foo in other;
    }
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  assertFalse(a.m({}));
  assertThrows(() => { a.m(100); }, TypeError);
  assertThrows(() => { a.m('foo'); }, TypeError);
  assertThrows(() => { a.m(undefined); }, TypeError);
  assertThrows(() => { a.m(null); }, TypeError);

  class B {
    get #foo() {}
    set #foo(a) {}
  }
  assertFalse(a.m(new B()));
})();

(function TestPrivateIdentifiersAreDistinct() {
  function GenerateClass() {
    class A {
      m(other) {
        return #x in other;
      }
      #x = 0;
    }
    return new A();
  }
  let a1 = GenerateClass();
  let a2 = GenerateClass();
  assertTrue(a1.m(a1));
  assertFalse(a1.m(a2));
  assertFalse(a2.m(a1));
  assertTrue(a2.m(a2));
})();

(function TestSubclasses() {
  class A {
    m(other) { return #foo in other; }
    #foo;
  }
  class B extends A {}
  assertTrue((new A()).m(new B()));
})();

(function TestFakeSubclassesWithField() {
  class A {
    #foo;
    m() { return #foo in this; }
  }
  let a = new A();
  assertTrue(a.m());

  // Plug an object into the prototype chain; it's not a real instance of the
  // class.
  let fake = {__proto__: a};
  assertFalse(fake.m());
})();

(function TestFakeSubclassesWithPrivateMethod() {
  class A {
    #pm() {}
    m() { return #pm in this; }
  }
  let a = new A();
  assertTrue(a.m());

  // Plug an object into the prototype chain; it's not a real instance of the
  // class.
  let fake = {__proto__: a};
  assertFalse(fake.m());
})();

(function TestPrivateNameUnknown() {
  assertThrows(() => { eval(`
  class A {
    m(other) { return #lol in other; }
  }
  new A().m();
  `)}, SyntaxError, /must be declared in an enclosing class/);
})();

// FIXME: inside eval?
// FIXME: private static?

// FIXME: tests for combining with other expressions?
// FIXME: test262?

// #foo in {} in {} in {}
// #foo in {} < a + 1
// #foo in {} instanceof 55

// Invalid: #foo in {} = 4 or (#foo in {}) = 4

// FIXME: check error message positions

// FIXME: static private methods
// FIXME: static private methods for the scope->class_variable() == nullptr case, when does that happen?

// FIXME: make sure "nonprivatemethod in this" still works

// FIXME: partially initialized objects

// FIXME: test error messages

// FIXME: for the "have feedback vector" and "don't have feedback vector" cases

// FIXME: for cases where we get feedback (what kind of handlers will we have?)

// FIXME: optimized code

// FIXME: #foo in my_proxy
