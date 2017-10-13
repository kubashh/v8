// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

var size = 1024*1024*1024 + 1; // 1 GB + 1 byte
(function() {
  let arr = new Uint8Array(size);
  assertThrows(() => WebAssembly.validate(arr), RangeError);
})();
gc();
