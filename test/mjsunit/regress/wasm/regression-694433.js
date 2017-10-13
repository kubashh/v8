// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

var size = Math.floor(0xFFFFFFFF / 4) + 1;
(function() {
  let arr = new Uint16Array(size);
  assertThrows(() => WebAssembly.validate(arr), RangeError);
})();
gc();
