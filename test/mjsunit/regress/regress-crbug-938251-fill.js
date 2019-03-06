// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let array = [];
array.length = 0xffffffff;

array.fill(1.1, 0, {valueOf() {
  array.length = 32;
  array.fill(1.1);
  return 0x80000000;
}});
