// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt


// Test CloneFastJSArray inserted by JSCallReducer for Array.prototype.slice.
// CloneFastJSArray produces COW arrays if the original array is COW.




// JSCallReducer will not reduce to CloneFastJSArray
// if the Array prototype is not original
// Check: isolate->IsAnyInitialArrayPrototype(receiver_prototype)
(function () {
  var array = [6, , 6];

  function slice() {
    return array.slice();
  }

  %PrepareFunctionForOptimization(slice);

  assertEquals(slice(),array);
  slice();

  // change the prototype
  array.__proto__ = [ , 6, ];

  %OptimizeFunctionOnNextCall(slice);
  let narr = slice();
  // if optimized, we would get [6, , 6]
  assertNotEquals(Object.getOwnPropertyDescriptor(narr, 1), undefined);
  assertEquals(narr, [6,6,6]);
})();
