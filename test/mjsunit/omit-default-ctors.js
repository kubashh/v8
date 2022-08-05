// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --omit-default-ctors

// FIXME: base class ctor called

(function BaseFunctionCalledWhenConstructingAnObject() {
  let baseFunctionCalled = false;
  function BaseFunction() { baseFunctionCalled = true; }
  class C extends BaseFunction {}

  assertFalse(baseFunctionCalled);
  new C();
  assertTrue(baseFunctionCalled);
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
