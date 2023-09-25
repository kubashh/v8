// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --allow-natives-syntax

// Access elements with `DataView.prototype.getInt32()`

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const start = performance.now();
function time(name) {
  const ms_since_start = (performance.now() - start).toFixed(1).padStart(7);
  print(`[${ms_since_start}] ${name}`);
}

(function TestGetInt32() {
  print(arguments.callee.name);
  const { buffer } = new Int32Array([0, 1, 2, 3, 4, 5]);
  const dataview = new DataView(buffer);

  let result;

  for (let counter = 0; counter < 10000; ++counter) {
    for (let i = 0; i < 5000; i++) {
        result += dataview.getInt32(i % 6);
    }
  }

  time("Function finished\n")
  return result;
})();

// Time: 52.7 ms.
