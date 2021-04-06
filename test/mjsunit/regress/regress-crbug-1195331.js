// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let o1 = { a: 0, b: 0 };
let o2 = { a: 0, b: 0 };
Object.defineProperty(o1, "b", {
  value: 4.2, enumerable: true, configurable: true, writable: true,
});

let o3 = { a: "foo", b: 0 };
Object.defineProperty(o2, "a", {
  enumerable: false, configurable: true, writable: true,
});
