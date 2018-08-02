// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

assertThrows(() => {
  Array.prototype.lastIndexOf.call(null, 42);
}, TypeError);
assertThrows(() => {
  Array.prototype.lastIndexOf.call(undefined, 42);
}, TypeError);

/* Tests inspired by Test262 */
// Stateful fromIndex that tries to empty the array
function testfromIndex(value) {
  var array = [5, undefined, 7];
  var fromIndex = {
    valueOf: function() {
      array.length = 0;
      return 2;
    }
  };
  assertEquals(-1, array.lastIndexOf(value, fromIndex));
}

testfromIndex(5);
testfromIndex(undefined);

// Stateful fromIndex and Prototype's proxy
// Must test for [[HasProperty]] before [[Get]]
function testHasProperty(value) {
  var array = [5, undefined, 7];
  var fromIndex = {
    valueOf: function() {
      array.length = 0;
      return 2;
    }
  };

  // The Prototype only has [[HasProperty]], no [[Get]]
  Object.setPrototypeOf(array,
    new Proxy(Array.prototype, {
                has: function(t, pk) { return pk in t; },
                get: function () { throw new Error('[[Get]] trap called') },
              }));

  assertEquals(-1, Array.prototype.lastIndexOf.call(array, value, fromIndex));
}

testHasProperty(5);
testHasProperty(undefined);

// Call order: [[HasProperty]] before [[Get]]
// Must test for [[HasProperty]] before [[Get]]
function testHasPropertyThenGet(value) {
  var array = [5, , 7];
  // The Prototype only has [[HasProperty]] and [[Get]]
  var olog = [];

  // the 2nd element (index 1) will trigger the calls to the prototype
  Object.setPrototypeOf(array,
    new Proxy(Array.prototype, {
                has: function(t, pk) { olog.push("HasProperty"); return true; },
                get: function (t, pk, rc) { olog.push("Get"); },
              }));

  Array.prototype.lastIndexOf.call(array, value);
  assertEquals(["HasProperty", "Get"], olog);
}

testHasPropertyThenGet(5);
testHasPropertyThenGet(undefined);
