// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --noenable-slow-asserts

var arr = [1];
for (var i = 1; i < 30; ++i) {
    var a2 = arr.map(function(){arr.push(2, 3, 4, 5, 6, 7);});
    arr.some(arr.constructor);
    for (var j = 0; j < 10000; ++j) {}
}
