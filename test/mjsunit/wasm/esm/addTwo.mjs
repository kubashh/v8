// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-esm

import {addTwo, mem, get1} from "./addTwo.wasm";

(function TestExportFunction() {
  assertEquals(typeof addTwo, "function");
})();

(function TestExportMemory() {
  assertInstanceof(mem, WebAssembly.Memory);
})();

(function TestExportGlobal() {
  assertInstanceof(get1, WebAssembly.Global);
  assertEquals(get1.value, 1);
})();
