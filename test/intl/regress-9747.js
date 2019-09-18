// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let lf = new Intl.ListFormat("en");

// Test normal array
assertDoesNotThrow(() => lf.format(['a','b','c']));
assertThrows("lf.format(['a','b',3])",  TypeError, "invalid_argument");

// Test sparse array
let sparse = ['a','b'];
sparse[10] = 'c';
assertThrows("lf.format(sparse)",  TypeError, "invalid_argument");

// Test iteratable of all String
let iteratable_of_strings = {
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
assertDoesNotThrow(() => lf.format(iteratable_of_strings));

// Test iteratable of none String throw TypeError
let iteratable_of_strings_and_number = {
  [Symbol.iterator]() {
    return this;
  },
  count: 0,
  next() {
    if (this.count++ < 4) {
      return {done: false, value: String(this.count)};
    }
    // Return number before done should throw TypeError.
    return {done:false, value: 3};
  }
};
assertThrows("lf.format(iteratable_of_strings_and_number)",
    TypeError, "invalid_argument");
