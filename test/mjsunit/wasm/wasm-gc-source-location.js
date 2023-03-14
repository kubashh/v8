// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(mliedtke): If the trap handler is enabled, source locations for null
// checks (and possibly for array bounds check as well) are not correct.
// Flags: --experimental-wasm-gc --no-wasm-trap-handler

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function TestStackTrace(testFct, trap, expected) {
  assertTraps(trap, testFct);
  try {
    testFct();
    assertUnreachable();
  } catch(e) {
    let regex = /at [^ ]+ \(wasm[^\[]+\[[0-9+]\]:(0x[0-9a-f]+)\)/;
    let match = e.stack.match(regex);
    assertEquals(expected, match[1])
  }
}

(function CodeLocationRefCast() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let otherStruct = builder.addStruct([makeField(kWasmI64, true)]);

  builder.addFunction('createStruct', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
      kGCPrefix, kExprExternExternalize,
    ])
    .exportFunc();

  builder.addFunction('createOther', makeSig([kWasmI64], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, otherStruct,
      kGCPrefix, kExprExternExternalize,
    ])
    .exportFunc();

  builder.addFunction('main', makeSig([kWasmExternRef], []))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCastNull, kStructRefCode,  // abstract cast
      kGCPrefix, kExprRefCastNull, struct,          // type cast
      kGCPrefix, kExprRefCast, struct,              // null check
      kExprDrop,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;
  let other = wasm.createOther(5n);

  TestStackTrace(() => wasm.main(3), kTrapIllegalCast, '0x6f');
  TestStackTrace(() => wasm.main(other), kTrapIllegalCast, '0x72');
  TestStackTrace(() => wasm.main(null), kTrapIllegalCast, '0x75');
})();

(function CodeLocationArrayLen() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction('arrayLen', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprExternInternalize,
      kGCPrefix, kExprRefCastNull, kArrayRefCode,
      kGCPrefix, kExprArrayLen,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  TestStackTrace(() => wasm.arrayLen(null), kTrapNullDereference, '0x2e');
})();
