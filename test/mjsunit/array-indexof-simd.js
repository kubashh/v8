// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Large array of Smi, and Smi search_element
(() => {
  let a = [];
  for (let i = 0; i < 200; i++) {
    a[i] = i;
  }
  function testArrayIndexOf(idx) {
    return a.indexOf(idx);
  }
  // Without fromIndex
  for (let i = 0; i < 200; i++) {
    assertEquals(i, testArrayIndexOf(i));
  }
  // With fromIndex
  for (let i = 0, from_index = 0; i+from_index < 200; i += 2, from_index++) {
    assertEquals(i, testArrayIndexOf(i, from_index));
  }
})();

// Large array of Doubles, and Double search_element
(() => {
  let a = [];
  for (let i = 0; i < 200; i++) {
    a[i] = i + 0.5;
  }
  function testArrayIndexOf(idx) {
    return a.indexOf(idx);
  }
  // Without fromIndex
  for (let i = 0; i < 200; i++) {
    assertEquals(i, testArrayIndexOf(i + 0.5));
  }
  // With fromIndex
  for (let i = 0, from_index = 0; i+from_index < 200; i += 2, from_index++) {
    assertEquals(i, testArrayIndexOf(i+0.5, from_index));
  }
})();

// Large array of objects, and object search_element
(() => {
  let a = [];
  let b = [];
  for (let i = 0; i < 200; i++) {
    a[i] = { v: i };
    b[i] = a[i];
  }
  function testArrayIndexOf(idx) {
    return a.indexOf(idx);
  }
  // Without fromIndex
  for (let i = 0; i < 200; i++) {
    assertEquals(i, testArrayIndexOf(b[i]));
  }
  // With fromIndex
  for (let i = 0, from_index = 0; i+from_index < 200; i += 2, from_index++) {
    assertEquals(i, testArrayIndexOf(b[i], from_index));
  }
})();
