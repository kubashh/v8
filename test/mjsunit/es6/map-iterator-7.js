// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-stress-opt

var keyValuePairs = [1,2,3].map(x => [x, x+1]);
var map = new Map(keyValuePairs);

assertEquals([[1,2], [2,3], [3,4]], [...map]);
assertEquals([[1,2], [2,3], [3,4]], [...map.entries()]);
assertEquals([1,2,3], [...map.keys()]);
assertEquals([2,3,4], [...map.values()]);
assertTrue(%MapIteratorProtector());

// This changes %MapIteratorPrototype%. No more tests should be run after this
// in the same instance.
var iterator = map.entries();
iterator.__proto__.next = () => ({value : undefined, done : true});

assertFalse(%MapIteratorProtector());
assertEquals([], [...map]);
assertEquals([], [...map.entries()]);
assertEquals([], [...map.keys()]);
assertEquals([], [...map.values()]);
assertEquals([], [...iterator]);
