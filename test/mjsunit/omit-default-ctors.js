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
  class Base {
    constructor() {
      ++ctorCallCount;
      this.baseTagged = true;
    }
  }
  class A extends Base {}
  const a = new A();
  assertEquals(1, ctorCallCount);
  assertTrue(a.baseTagged);
  class B extends A {}
  const b = new B();
  assertEquals(2, ctorCallCount);
  assertTrue(b.baseTagged);
})();

(function NonDefaultDerivedConstructorCalled() {
  let ctorCallCount = 0;
  class Base {}
  class Derived extends Base {
    constructor() {
      super();
      ++ctorCallCount;
      this.derivedTagged = true;
    }
  }
  class A extends Derived {}
  const a = new A();
  assertEquals(1, ctorCallCount);
  assertTrue(a.derivedTagged);
  class B extends A {}
  const b = new B();
  assertEquals(2, ctorCallCount);
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


// FIXME: test: passing args

// FIXME: test: instance field initializer

// FIXME: test: brand

// FIXME: test: super class ctor not a ctor (and the default ctor which should detect it is omitted)

/*

super(foo()); -> foo called even if super class ctor is not a ctor

*/

// FIXME: test that the returned objects are "reasonable" (like, not undefined, for example)
// and that the __proto__ etc are correct.

// Can we test the .constructor of the map - it should be the superclass ctor, no?

// FIXME: test: changing the superclass, seeing that the rest parameters are handled correctly
