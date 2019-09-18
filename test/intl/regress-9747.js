// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let lf = new Intl.ListFormat("en");

// Test normal array
assertDoesNotThrow(() => lf.format(['a','b','c']));
assertThrows("lf.format(['a','b',3])",  TypeError, "Wrong Error Message1");

// Test sparse array
let sparse = ['a','b'];
sparse[10] = 'c';
assertThrows("lf.format(sparse)",  TypeError, "Wrong Error Message2");

// Test iteratable of all String
let it1 = {
  [Symbol.iterator]() {
    return this;
  },
  count: 0,
  next() {
    if (this.count++ < 4) {
      return {done: false, value: String(this.count)};
    }
    return {done:true}
  }
};
assertDoesNotThrow(() => lf.format(it1));

// Test iteratable of none String throw TypeError
let it2 = {
  [Symbol.iterator]() {
    return this;
  },
  count: 0,
  next() {
    if (this.count++ < 4) {
      return {done: false, value: String(this.count)};
    }
    // Return non String before done should throw TypeError.
    return {done:false, value: 3};
  }
};
assertThrows("lf.format(it2)",  TypeError, "Wrong Error Message3");
