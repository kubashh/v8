// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-change-array-by-copy

assertEquals(Array.prototype.toReversed.length, 0);
assertEquals(Array.prototype.toReversed.name, "toReversed");

(function TestSmiPacked() {
  let a = [1,2,3,4];
  let r = a.toReversed();
  assertEquals([4,3,2,1], r);
  assertFalse(a === r);
})();

(function TestDoublePacked() {
  let a = [1.1,2.2,3.3,4.4];
  let r = a.toReversed();
  assertEquals([4.4,3.3,2.2,1.1], r);
  assertFalse(a === r);
})();

(function TestPacked() {
  let a = [true,false,1,42.42];
  let r = a.toReversed();
  assertEquals([42.42,1,false,true], r);
  assertFalse(a === r);
})();

(function TestGeneric() {
  let a = { length: 4,
            get "0"() { return "hello"; },
            get "1"() { return "cursed"; },
            get "2"() { return "java"; },
            get "3"() { return "script" } };
  let r = Array.prototype.toReversed.call(a);
  assertEquals(["script","java","cursed","hello"], r);
  assertFalse(a === r);
  assertTrue(Array.isArray(r));
  assertEquals(Array, r.constructor);
})();

(function TestNoSpecies() {
  class MyArray extends Array {
    static get [Symbol.species]() { return MyArray; }
  }
  assertEquals(Array, (new MyArray()).toReversed().constructor);
})();

(function TestNoHoles() {
  let a = [,,,,];
  let r = a.toReversed();
  assertEquals([undefined,undefined,undefined,undefined], r);
  assertTrue(r.hasOwnProperty('0'));
  assertTrue(r.hasOwnProperty('1'));
  assertTrue(r.hasOwnProperty('2'));
  assertTrue(r.hasOwnProperty('3'));
})();
