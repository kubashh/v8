// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

let k1MiB = 1 * 1024 * 1024;
let k1GiB = 1 * 1024 * 1024 * 1024;
let k4GiB = 4 * k1GiB;
let kPageSize = 65536;
let kMaxMemory = 2 * k1GiB - kPageSize; // TODO(titzer): raise this to 4GiB
var memory;
try {
  let kPages = kMaxMemory / kPageSize;
  memory = new WebAssembly.Memory({initial: kPages, maximum: kPages});
} catch (e) {
  print("OOM: sorry, best effort max memory size test!");
  throw e;
}

// Check the size of the underlying buffer
let buffer = memory.buffer;
assertEquals(kMaxMemory, buffer.byteLength);

function probe(view, stride, f) {
  let max = view.length;
  for (let i = 0; i < max; i += stride) {
    view[i] = f(i);
  }
  for (let i = 0; i < max; i += stride) {
//    print(`${i} = ${f(i)}`);
    assertEquals(f(i), view[i]);
  }
}

(function TestUint32View() {
  // Make an uint32 view and probe it.
  let elemSize = 4;
  let uint32 = new Uint32Array(buffer);
  assertEquals(kMaxMemory / elemSize, uint32.length);
  probe(uint32, kPageSize / elemSize, i => (0xaabbccee ^ ((i >> 11) * 0x110005)) >>> 0);
})();

(function TestUint16View() {
  // Make an uint16 view and probe it.
  // TODO(titzer): hits SMI-limit on the length of Uint16Array
  let elemSize = 2;
  let uint16 = new Uint16Array(buffer);
  assertEquals(kMaxMemory / elemSize, uint16.length);
  probe(uint16, kPageSize / elemSize, i => (0xccee ^ ((i >> 11) * 0x110005)) & 0xFFFF);
})();

(function TestUint8View() {
  // Make an uint8 view and probe it.
  // TODO(titzer): hits SMI-limit on the length of Uint8Array
  let elemSize = 1;
  let uint8 = new Uint8Array(buffer);
  assertEquals(kMaxMemory / elemSize, uint8.length);
  probe(uint8, kPageSize / elemSize, i => (0xee ^ ((i >> 11) * 0x05)) & 0xFF);
})();

(function TestFloat64View() {
  // Make a float64 view and probe it.
  let elemSize = 8;
  let float64 = new Float64Array(buffer);
  assertEquals(kMaxMemory / elemSize, float64.length);
  probe(float64, kPageSize / elemSize, i => 0xaabbccee ^ ((i >> 11) * 0x110005));
})();
