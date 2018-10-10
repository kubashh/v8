// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-stress-opt

// This tests the interaction between the MapIterator protector and SetIterator
// protector.

var keyValuePairs = [1,2,3].map(x => [x, x+1]);
var map = new Map(keyValuePairs);

assertEquals([[1,2], [2,3], [3,4]], [...map]);
assertEquals([[1,2], [2,3], [3,4]], [...map.entries()]);
assertEquals([1,2,3], [...map.keys()]);
assertEquals([2,3,4], [...map.values()]);
assertTrue(%MapIteratorProtector());

var set = new Set([1,2,3]);
assertEquals([[1,1],[2,2],[3,3]], [...set.entries()]);
assertEquals([1,2,3], [...set]);
assertEquals([1,2,3], [...set.keys()]);
assertEquals([1,2,3], [...set.values()]);
assertTrue(%SetIteratorProtector());

// This changes %IteratorPrototype%. No more tests should be run after this in
// the same instance.
var iterator = map.keys();
// iterator object --> %MapIteratorPrototype% --> %IteratorPrototype%
iterator.__proto__.__proto__[Symbol.iterator] =
  () => ({next: () => ({value : undefined, done : true})});

assertFalse(%MapIteratorProtector());
assertEquals([[1,2], [2,3], [3,4]], [...map]);
assertEquals([], [...map.entries()]);
assertEquals([], [...map.keys()]);
assertEquals([], [...map.values()]);
assertEquals([], [...iterator]);

assertFalse(%SetIteratorProtector());
assertEquals([], [...set.entries()]);
assertEquals([1,2,3], [...set]);
assertEquals([], [...set.keys()]);
assertEquals([], [...set.values()]);
