// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --experimental-wasm-eh --experimental-wasm-gc --experimental-wasm-stringref

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function assertValid(fn) {
  let builder = new WasmModuleBuilder();
  fn(builder);
  assertTrue(WebAssembly.validate(builder.toBuffer()));
}

// TODO(wingo): Enable when we start parsing string literal sections.
// assertValid(builder => builder.addLiteralStringRef("foo"));

for (let [name, code] of [['string', kWasmStringRef],
                          ['stringview_wtf8', kWasmStringViewWtf8],
                          ['stringview_wtf16', kWasmStringViewWtf16],
                          ['stringview_iter', kWasmStringViewIter]]) {
  let default_init = WasmInitExpr.RefNull(code);

  assertValid(b => b.addType(makeSig([code], [])));
  assertValid(b => b.addStruct([makeField(code, true)]));
  assertValid(b => b.addArray(code, true));
  assertValid(b => b.addType(makeSig([], [code])));
  assertValid(b => b.addGlobal(code, true, default_init));
  // TODO(wingo): Table of strings not yet implemented.
  // assertValid(b => b.addTable(code, 0));
  assertValid(b => b.addPassiveElementSegment([default_init], code));
  assertValid(b => b.addTag(makeSig([code], [])));
  assertValid(
    b => b.addFunction(undefined, kSig_v_v).addLocals(code, 1).addBody([]));
}

(function TestInstructions() {
  let builder = new WasmModuleBuilder();
  let kSig_ii_s = makeSig([kWasmI32, kWasmI32], [kWasmStringRef]);
  let kSig_v_s = makeSig([], [kWasmStringRef]);
  let kSig_s_i = makeSig([kWasmStringRef], [kWasmI32]);
  let kSig_si_v = makeSig([kWasmStringRef, kWasmI32], []);
  let kSig_ss_s = makeSig([kWasmStringRef, kWasmStringRef], [kWasmStringRef]);
  let kSig_ss_i = makeSig([kWasmStringRef, kWasmStringRef], [kWasmI32]);
  let kSig_s_x = makeSig([kWasmStringRef], [kWasmStringViewWtf8]);
  let kSig_xii_i = makeSig([kWasmStringViewWtf8, kWasmI32, kWasmI32],
                           [kWasmI32]);
  let kSig_xiii_ii = makeSig([kWasmStringViewWtf8, kWasmI32, kWasmI32,
                              kWasmI32],
                             [kWasmI32, kWasmI32]);
  let kSig_xii_s = makeSig([kWasmStringViewWtf8, kWasmI32, kWasmI32],
                           [kWasmStringRef]);
  let kSig_s_y = makeSig([kWasmStringRef], [kWasmStringViewWtf16]);
  let kSig_y_i = makeSig([kWasmStringViewWtf16], [kWasmI32]);
  let kSig_yi_i = makeSig([kWasmStringViewWtf16, kWasmI32], [kWasmI32]);
  let kSig_yiii_v = makeSig([kWasmStringViewWtf16, kWasmI32, kWasmI32,
                             kWasmI32], []);
  let kSig_yii_s = makeSig([kWasmStringViewWtf16, kWasmI32, kWasmI32],
                           [kWasmStringRef]);
  let kSig_s_z = makeSig([kWasmStringRef], [kWasmStringViewIter]);
  let kSig_z_i = makeSig([kWasmStringViewIter], [kWasmI32]);
  let kSig_zi_i = makeSig([kWasmStringViewIter, kWasmI32], [kWasmI32]);
  let kSig_zi_s = makeSig([kWasmStringViewIter, kWasmI32],
                          [kWasmStringRef]);

  builder.addMemory(0, undefined, false, false);

  builder.addFunction("string.new_wtf8", kSig_ii_s)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringNewWtf8, 0
    ]);

  builder.addFunction("string.new_wtf16", kSig_ii_s)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringNewWtf16, 0
    ]);

  // TODO(wingo): Enable when we start parting string literal sections.
  // builder.addFunction("string.const", kSig_v_s)
  //   .addLiteralStringRef("foo")
  //   .addBody([
  //     kGCPrefix, kExprStringConst, 0
  //   ]);

  builder.addFunction("string.measure_utf8", kSig_s_i)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringMeasureUtf8
    ]);

  builder.addFunction("string.measure_wtf8", kSig_s_i)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringMeasureWtf8
    ]);

  builder.addFunction("string.measure_wtf16", kSig_s_i)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringMeasureWtf16
    ]);

  builder.addFunction("string.encode_wtf8/utf-8", kSig_si_v)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringEncodeWtf8, 0, 0
    ]);
  builder.addFunction("string.encode_wtf8/wtf-8", kSig_si_v)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringEncodeWtf8, 0, 1
    ]);
  builder.addFunction("string.encode_wtf8/replace", kSig_si_v)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringEncodeWtf8, 0, 2
    ]);

  builder.addFunction("string.encode_wtf16", kSig_si_v)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringEncodeWtf16, 0
    ]);

  builder.addFunction("string.concat", kSig_ss_s)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringConcat
    ]);

  builder.addFunction("string.eq", kSig_ss_i)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringEq
    ]);

  builder.addFunction("string.as_wtf8", kSig_s_x)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringAsWtf8
    ]);

  builder.addFunction("stringview_wtf8.advance", kSig_xii_i)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
      kGCPrefix, kExprStringViewWtf8Advance
    ]);

  builder.addFunction("stringview_wtf8.encode/utf-8", kSig_xiii_ii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2, kExprLocalGet, 3,
      kGCPrefix, kExprStringViewWtf8Encode, 0, 0
    ]);

  builder.addFunction("stringview_wtf8.encode/wtf-8", kSig_xiii_ii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2, kExprLocalGet, 3,
      kGCPrefix, kExprStringViewWtf8Encode, 0, 1
    ]);

  builder.addFunction("stringview_wtf8.encode/replace", kSig_xiii_ii)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2, kExprLocalGet, 3,
      kGCPrefix, kExprStringViewWtf8Encode, 0, 2
    ]);

  builder.addFunction("stringview_wtf8.slice", kSig_xii_s)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
      kGCPrefix, kExprStringViewWtf8Slice
    ]);

  builder.addFunction("string.as_wtf16", kSig_s_y)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringAsWtf16
    ]);

  builder.addFunction("stringview_wtf16.length", kSig_y_i)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringViewWtf16Length
    ]);

  builder.addFunction("stringview_wtf16.get_codeunit", kSig_yi_i)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringViewWtf16GetCodeunit
    ]);

  builder.addFunction("stringview_wtf16.encode", kSig_yiii_v)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2, kExprLocalGet, 3,
      kGCPrefix, kExprStringViewWtf16Encode, 0
    ]);

  builder.addFunction("stringview_wtf16.slice", kSig_yii_s)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
      kGCPrefix, kExprStringViewWtf16Slice
    ]);

  builder.addFunction("string.as_iter", kSig_s_z)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringAsIter
    ]);

  builder.addFunction("stringview_iter.cur", kSig_z_i)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStringViewIterCur
    ]);

  builder.addFunction("stringview_iter.advance", kSig_zi_i)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringViewIterAdvance
    ]);

  builder.addFunction("stringview_iter.rewind", kSig_zi_i)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringViewIterRewind
    ]);

  builder.addFunction("stringview_iter.slice", kSig_zi_s)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1,
      kGCPrefix, kExprStringViewIterSlice
    ]);

  assertTrue(WebAssembly.validate(builder.toBuffer()));
})();
