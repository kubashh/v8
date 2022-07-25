// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-return-call --experimental-wasm-gc

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestCallIndirectJSFunction() {
  print(arguments.callee.name);
  let js_function = function(a, b, c) { return c ? a : b; };

  let test = function(is_tail_call) {
    const builder = new WasmModuleBuilder();

    builder.addType(kSig_i_i);
    let sig = builder.addType(kSig_i_iii);

    let callee = builder.addImport("m", "f", kSig_i_iii);

    let table = builder.addTable(kWasmFuncRef, 10, 10);

    builder.addActiveElementSegment(table, wasmI32Const(0), [callee]);

    let left = -2;
    let right = 3;

    builder.addFunction("main", kSig_i_i)
      .addBody([...wasmI32Const(left), ...wasmI32Const(right), kExprLocalGet, 0,
                ...wasmI32Const(0),
                is_tail_call ? kExprReturnCallIndirect : kExprCallIndirect,
                sig, table.index])
      .exportFunc();

    let instance = builder.instantiate({m: {f: js_function}});

    assertEquals(left, instance.exports.main(1));
    assertEquals(right, instance.exports.main(0));
  }

  test(true);
  test(false);
})();

(function TestCallIndirectCrossModuleTypeCanonicalization() {
  print(arguments.callee.name);

  let exporting_instance = (function() {
    const builder = new WasmModuleBuilder();

    builder.setSingletonRecGroups();

    let struct = builder.addStruct([makeField(kWasmI32, true)]);

    let sig = builder.addType(makeSig([wasmRefNullType(struct)], [kWasmI32]));

    builder.addFunction("export", sig)
      .addBody([kExprLocalGet, 0, kGCPrefix, kExprStructGet, struct, 0,
                ...wasmI32Const(0), kExprI32GeS])
      .exportFunc();

    return builder.instantiate();
  })();

  let importing_instance = (function() {
    const builder = new WasmModuleBuilder();

    builder.setSingletonRecGroups();

    // Just so the canonicalized struct has a different index in this module.
    builder.addStruct([]);
    let struct = builder.addStruct([makeField(kWasmI32, true)]);

    let sig = builder.addType(makeSig([wasmRefNullType(struct)], [kWasmI32]));

    builder.addImport("m", "f", sig);

    builder.addFunction("local", sig)
      .addBody([kExprLocalGet, 0, kGCPrefix, kExprStructGet, struct, 0,
                ...wasmI32Const(0), kExprI32LtS]);

    builder.addFunction("mistyped", kSig_i_i).addBody([kExprLocalGet, 0])

    let table = builder.addTable(kWasmFuncRef, 10, 10);

    builder.addActiveElementSegment(
      table.index, wasmI32Const(0),
      [[kExprRefFunc, 0], [kExprRefFunc, 1], [kExprRefFunc, 2]],
      table.type);

    // Struct field, table index -> struct field >= 0 (or < 0)
    builder.addFunction("main", kSig_i_ii)
      .addBody([kExprLocalGet, 0, kGCPrefix, kExprStructNew, struct,
                kExprLocalGet, 1, kExprCallIndirect, sig, table.index])
      .exportFunc();

    return builder.instantiate({m: {f: exporting_instance.exports.export}});
  })();

  assertEquals(1, importing_instance.exports.main(10, 0))
  assertEquals(0, importing_instance.exports.main(10, 1))
  assertEquals(0, importing_instance.exports.main(-5, 0))
  assertEquals(1, importing_instance.exports.main(-5, 1))
  // Mistyped entry
  assertTraps(kTrapFuncSigMismatch,
              () => importing_instance.exports.main(10, 2));
  // Null entry
  assertTraps(kTrapFuncSigMismatch,
              () => importing_instance.exports.main(10, 3));
  assertTraps(kTrapTableOutOfBounds,
              () => importing_instance.exports.main(10, 10));
})();
