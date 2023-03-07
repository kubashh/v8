// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const GB = 1024 * 1024 * 1024;
const SRC_OFFSET = 4294970000n; // 0x100000a90n
const SRC_OFFSET_LEB = [0x90, 0x95, 0x80, 0x80, 0x10];
const DST_OFFSET = 4294970160n;
const DST_OFFSET_LEB = [0xb0, 0x96, 0x80, 0x80, 0x10];

var builder = new WasmModuleBuilder();
builder.addMemory64(5 * GB / kPageSize).exportMemoryAs("memory");

// Here we make a global of type v128 to be the target
// for loading lanes and the source for storing lanes.
var g = builder.addGlobal(
  kWasmS128, true,
  [kSimdPrefix, kExprS128Const,
   1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0]);

for (let i = 0; i < 4; ++i) {
  builder.addFunction(`load_lane_${i}`, kSig_v_l)
      .addBody([kExprLocalGet, 0,
                kExprGlobalGet, g.index,
                kSimdPrefix, kExprS128Load32Lane, 0, 0, i,
                kExprGlobalSet, g.index])
      .exportFunc();

  builder.addFunction(`store_lane_${i}`, kSig_v_l)
      .addBody([kExprLocalGet, 0,
                kExprGlobalGet, g.index,
                kSimdPrefix, kExprS128Store32Lane, 0, 0, i])
      .exportFunc();

  builder.addFunction(`Load_Lane_${i}`, kSig_v_l)
      .addBody([kExprLocalGet, 0,
                kExprGlobalGet, g.index,
                kSimdPrefix, kExprS128Load32Lane, 0, ...SRC_OFFSET_LEB, i,
                kExprGlobalSet, g.index])
      .exportFunc();

  builder.addFunction(`Store_Lane_${i}`, kSig_v_l)
      .addBody([kExprLocalGet, 0,
                kExprGlobalGet, g.index,
                kSimdPrefix, kExprS128Store32Lane, 0, ...DST_OFFSET_LEB, i])
      .exportFunc();
}

var module = builder.instantiate({});
var buffer_view = new Int32Array(module.exports.memory.buffer);

var values = [ 0x0123, 0x4567, 0x89ab, 0xcdef ];

function set_buffer() {
  for (let i = 0n; i < 4n; ++i ) {
    buffer_view[ (SRC_OFFSET + 4n * i) / 4n] = values[i];
  }
}

set_buffer();
for (let i = 0n; i < 4n; ++i) {
  const offset = 4n * i;
  module.exports[`load_lane_${i}`] (SRC_OFFSET + offset);
  module.exports[`store_lane_${i}`](DST_OFFSET + offset);
  assertEquals(values[i], buffer_view[(SRC_OFFSET + offset) / 4n]);
  assertEquals(values[i], buffer_view[(DST_OFFSET + offset) / 4n]);
}

values = [ 0xbeef, 0xfbee, 0xefbe, 0xeefb ];
set_buffer();

for (let i = 0n; i < 4n; ++i) {
  const offset = 4n * i;
  module.exports[`Load_Lane_${i}`](offset);
  module.exports[`Store_Lane_${i}`](offset);
  assertEquals(values[i], buffer_view[ (SRC_OFFSET + offset) / 4n]);
  assertEquals(values[i], buffer_view[ (DST_OFFSET + offset) / 4n]);
}
