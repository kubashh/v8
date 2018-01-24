// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function f() { return 42; }

Set.prototype.add = f.bind();
new Set([]);
new Set([{},{}]);

WeakSet.prototype.add = f.bind();
new WeakSet([]);
new WeakSet([{},{}]);

Map.prototype.set = f.bind();
new Map([]);
new Map([[{},{}],[{},{}]]);

WeakMap.prototype.set = f.bind();
new WeakMap([]);
new WeakMap([[{},{}],[{},{}]]);
