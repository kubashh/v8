// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var emptyArr = {};
var realmArr = Realm.eval(emptyArr, "Array");
Object.defineProperty(realmArr, 'isArray', { writable: true });
realmArr.isArray = function() {
};

const arr = new Float32Array(1);
arr.set([-NaN]);
print(new Uint32Array(arr.buffer));
