// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-threads

load("test/mjsunit/wasm/wasm-module-builder.js");

(function TestGrowingSharedMemory() {
  const argument = { "initial": 1, "maximum": 2, "shared": true };
  const memory = new WebAssembly.Memory(argument);
  const oldMemory = memory.buffer;

  const result = memory.grow(1);
  assertEquals(result, 1);

  const newMemory = memory.buffer;
  assertNotEquals(oldMemory, newMemory);
})();
