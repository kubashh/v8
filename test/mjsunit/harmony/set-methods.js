// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-set-methods

// --------- Test union method

(function TestUnion() {
  const firstSet = new Set();

  firstSet.add(1);
  firstSet.add(2);
  firstSet.add(3);

  const otherSet = new Set();

  otherSet.add(4);
  otherSet.add(5);
  otherSet.add(6);

  assertEquals({1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6}, firstSet.union(otherSet));
})();
