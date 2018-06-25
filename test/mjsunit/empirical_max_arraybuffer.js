// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let k1MiB = 1 * 1024 * 1024;
let k1GiB = 1 * 1024 * 1024 * 1024;
let k4GiB = 4 * k1GiB;
let kPageSize = 65536;
let kMaxArrayBufferSize = 2 * k1GiB - kPageSize; // TODO(titzer): raise this to 4GiB
let kPageSize = 65536;
var buffer;
try {
  buffer = new ArrayBuffer(kMaxArrayBufferSize);
} catch (e) {
  print("OOM: sorry, best effort max array buffer size test!");
  throw e;
}

// Check the size of the underlying buffer
assertEquals(kMaxArrayBufferSize, buffer.byteLength);

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
  assertEquals(kStrideLength / elemSize, uint32.length);
  probe(uint32, kPageSize / elemSize, i => (0xaabbccee ^ ((i >> 11) * 0x110005)) >>> 0);
})();

(function TestUint16View() {
  // Make an uint16 view and probe it.
  let elemSize = 2;
  let uint16 = new Uint16Array(buffer);
  assertEquals(kStrideLength / elemSize, uint16.length);
  probe(uint16, kPageSize / elemSize, i => (0xccee ^ ((i >> 11) * 0x110005)) & 0xFFFF);
})();

(function TestUint8View() {
  // Make an uint8 view and probe it.
  let elemSize = 1;
  let uint8 = new Uint8Array(buffer);
  assertEquals(kStrideLength / elemSize, uint8.length);
  probe(uint8, kPageSize / elemSize, i => (0xee ^ ((i >> 11) * 0x05)) & 0xFF);
})();

(function TestFloat64View() {
  // Make a float64 view and probe it.
  let elemSize = 8;
  let float64 = new Float64Array(buffer);
  assertEquals(kStrideLength / elemSize, float64.length);
  probe(float64, kPageSize / elemSize, i => 0xaabbccee ^ ((i >> 11) * 0x110005));
})();
