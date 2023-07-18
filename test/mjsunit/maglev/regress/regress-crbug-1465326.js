// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --maglev

class A {}

var x = Function;

class B extends A {
  constructor() {
    x = new.target;
    super();
  }
}
function construct() {
  return Reflect.construct(B, [], Function);
}
for (let i = 0; i < 2000; i++) construct();
var arr = construct();
console.log(arr.prototype);
