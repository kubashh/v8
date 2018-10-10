// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-stress-opt

var set = new Set([1,2,3]);

assertEquals([[1,1],[2,2],[3,3]], [...set.entries()]);
assertEquals([1,2,3], [...set]);
assertEquals([1,2,3], [...set.keys()]);
assertEquals([1,2,3], [...set.values()]);
assertTrue(%SetIteratorProtector());

// This changes %SetIteratorPrototype%. No more tests should be run after this
// in the same instance.
var iterator = set.keys();
iterator.__proto__.next = () => ({value : undefined, done : true});

assertFalse(%SetIteratorProtector());
assertEquals([], [...set.entries()]);
assertEquals([], [...set]);
assertEquals([], [...set.keys()]);
assertEquals([], [...set.values()]);
assertEquals([], [...iterator]);
