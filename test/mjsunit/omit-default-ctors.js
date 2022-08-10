// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --omit-default-ctors

(function OmitDefaultBaseCtor() {
  class A {}  // default base ctor -> will be omitted
  class B extends A {}
  const o = new B();
  assertSame(B.prototype, o.__proto__);
})();

(function OmitDefaultDerivedCtor() {
  class A { constructor() {} }
  class B extends A {}  // default derived ctor -> will be omitted
  class C extends B {}
  const o = new C();
  assertSame(C.prototype, o.__proto__);
})();

(function OmitDefaultBaseAndDerivedCtor() {
  class A {} // default base ctor -> will be omitted
  class B extends A {}  // default derived ctor -> will be omitted
  class C extends B {}
  const o = new C();
  assertSame(C.prototype, o.__proto__);
})();

(function OmitDefaultBaseCtorWithExplicitSuper() {
  class A {}  // default base ctor -> will be omitted
  class B extends A { constructor() { super(); } }
  const o = new B();
  assertSame(B.prototype, o.__proto__);
})();

(function OmitDefaultDerivedCtorWithExplicitSuper() {
  class A { constructor() {} }
  class B extends A {}  // default derived ctor -> will be omitted
  class C extends B { constructor() { super(); } }
  const o = new C();
  assertSame(C.prototype, o.__proto__);
})();

(function OmitDefaultBaseAndDerivedCtorWithExplicitSuper() {
  class A {} // default base ctor -> will be omitted
  class B extends A {}  // default derived ctor -> will be omitted
  class C extends B { constructor() { super(); } }
  const o = new C();
  assertSame(C.prototype, o.__proto__);
})();

(function NonDefaultBaseConstructorCalled() {
  let ctorCallCount = 0;
  let lastArgs;
  class Base {
    constructor(...args) {
      ++ctorCallCount;
      this.baseTagged = true;
      lastArgs = args;
    }
  }
  // Nothing will be skipped
  class A extends Base {}
  const a = new A(1, 2, 3);
  assertEquals(1, ctorCallCount);
  assertEquals([1, 2, 3], lastArgs);
  assertTrue(a.baseTagged);

  // 'A' default ctor will be skipped
  class B extends A {}
  const b = new B(4, 5, 6);
  assertEquals(2, ctorCallCount);
  assertEquals([4, 5, 6], lastArgs);
  assertTrue(b.baseTagged);
})();

(function NonDefaultDerivedConstructorCalled() {
  let ctorCallCount = 0;
  let lastArgs;
  class Base {}
  class Derived extends Base {
    constructor(...args) {
      super();
      ++ctorCallCount;
      this.derivedTagged = true;
      lastArgs = args;
    }
  }
  // Nothing will be skipped
  class A extends Derived {}
  const a = new A(1, 2, 3);
  assertEquals(1, ctorCallCount);
  assertEquals([1, 2, 3], lastArgs);
  assertTrue(a.derivedTagged);

  // 'A' default ctor will be skipped
  class B extends A {}
  const b = new B(4, 5, 6);
  assertEquals(2, ctorCallCount);
  assertEquals([4, 5, 6], lastArgs);
  assertTrue(b.derivedTagged);
})();

(function BaseFunctionCalled() {
  let baseFunctionCalled = false;
  function BaseFunction() {
    baseFunctionCalled = true;
    this.baseTagged = true;
  }
  class A extends BaseFunction {}

  assertFalse(baseFunctionCalled);
  let a = new A();
  assertTrue(baseFunctionCalled);
  assertTrue(a.baseTagged);
})();

(function NonSuperclassCtor() {
  class A {};
  class B extends A {};
  class C extends B {};
  class D extends C {};

  // Install an object which is not a constructor into the class hierarchy.
  C.__proto__ = {};

  assertThrows(() => { new C(); }, TypeError);
  assertThrows(() => { new D(); }, TypeError);
})();

(function ArgumentsEvaluatedBeforeNonSuperclassCtorDetected() {
  class A {};
  class B extends A {};
  class C extends B {};
  class D extends C {};

  // Install an object which is not a constructor into the class hierarchy.
  C.__proto__ = {};

  let callCount = 0;
  function foo() {
    ++callCount;
  }

  assertThrows(() => { new C(foo()); }, TypeError);
  assertEquals(1, callCount);

  assertThrows(() => { new D(foo()); }, TypeError);
  assertEquals(2, callCount);
})();

(function ArgumentsEvaluatedBeforeNonSuperclassCtorDetected2() {
  class A {};
  class B extends A {};
  class C extends B {};
  class D extends C {
    constructor() {
      super(foo());
    }
  };

  // Install an object which is not a constructor into the class hierarchy.
  C.__proto__ = {};

  let callCount = 0;
  function foo() {
    ++callCount;
  }

  assertThrows(() => { new D(); }, TypeError);
  assertEquals(1, callCount);
})();


// FIXME: test: instance field initializer

// FIXME: test: brand


/*

super(foo()); -> foo called even if super class ctor is not a ctor

*/

// FIXME: test that the returned objects are "reasonable" (like, not undefined, for example)
// and that the __proto__ etc are correct.

// Can we test the .constructor of the map - it should be the superclass ctor, no?

// FIXME: test: changing the superclass, seeing that the rest parameters are handled correctly
