// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-set-methods

// --------- Test union method

(function TestUnion() {
  const firstSet = new Set();
  firstSet.add(42);
  firstSet.add(43);
  firstSet.add(44);

  const otherSet = new Set();
  otherSet.add(45);
  otherSet.add(46);
  otherSet.add(47);

  const resultSet = new Set();
  resultSet.add(42);
  resultSet.add(43);
  resultSet.add(44);
  resultSet.add(45);
  resultSet.add(46);
  resultSet.add(47);

  const resultArray = Array.from(resultSet.keys());  // or .values();
  const unionArray = Array.from(firstSet.union(otherSet).keys())

  assertEquals(resultArray, unionArray);
})();
