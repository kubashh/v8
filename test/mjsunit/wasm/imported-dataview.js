// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// We use "r" for nullable "externref", and "e" for non-nullable "ref extern".
let kSig_l_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI64]);
let kSig_i_ri = makeSig([kWasmExternRef, kWasmI32], [kWasmI32]);
let kSig_i_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI32]);
let kSig_f_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmF32]);
let kSig_d_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmF64]);

let kSig_v_rili = makeSig([kWasmExternRef, kWasmI32, kWasmI64, kWasmI32], []);
let kSig_v_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], []);
let kSig_v_riii = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32], []);
let kSig_v_rifi = makeSig([kWasmExternRef, kWasmI32, kWasmF32, kWasmI32], []);
let kSig_v_ridi = makeSig([kWasmExternRef, kWasmI32, kWasmF64, kWasmI32], []);

let kDataViewGetBigInt64;
let kDataViewGetBigUint64;
let kDataViewGetFloat32;
let kDataViewGetFloat64;
let kDataViewGetInt8;
let kDataViewGetInt16;
let kDataViewGetInt32;
let kDataViewGetUint8;
let kDataViewGetUint16;
let kDataViewGetUint32;

let kDataViewSetBigInt64;
let kDataViewSetBigUint64;
let kDataViewSetFloat32;
let kDataViewSetFloat64;
let kDataViewSetInt8;
let kDataViewSetInt16;
let kDataViewSetInt32;
let kDataViewSetUint8;
let kDataViewSetUint16;
let kDataViewSetUint32;

function MakeBuilder() {
  let builder = new WasmModuleBuilder();

  kDataViewGetBigInt64 =
      builder.addImport('DataView', 'getBigInt64Import', kSig_l_rii);
  kDataViewGetBigUint64 =
      builder.addImport('DataView', 'getBigUint64Import', kSig_l_rii);
  kDataViewGetFloat32 =
      builder.addImport('DataView', 'getFloat32Import', kSig_f_rii);
  kDataViewGetFloat64 =
      builder.addImport('DataView', 'getFloat64Import', kSig_d_rii);
  kDataViewGetInt8 = builder.addImport('DataView', 'getInt8Import', kSig_i_ri);
  kDataViewGetInt16 =
      builder.addImport('DataView', 'getInt16Import', kSig_i_rii);
  kDataViewGetInt32 =
      builder.addImport('DataView', 'getInt32Import', kSig_i_rii);
  kDataViewGetUint8 =
      builder.addImport('DataView', 'getUint8Import', kSig_i_ri);
  kDataViewGetUint16 =
      builder.addImport('DataView', 'getUint16Import', kSig_i_rii);
  kDataViewGetUint32 =
      builder.addImport('DataView', 'getUint32Import', kSig_i_rii);

  kDataViewSetBigInt64 =
      builder.addImport('DataView', 'setBigInt64Import', kSig_v_rili);
  kDataViewSetBigUint64 =
      builder.addImport('DataView', 'setBigUint64Import', kSig_v_rili);
  kDataViewSetFloat32 =
      builder.addImport('DataView', 'setFloat32Import', kSig_v_rifi);
  kDataViewSetFloat64 =
      builder.addImport('DataView', 'setFloat64Import', kSig_v_ridi);
  kDataViewSetInt8 = builder.addImport('DataView', 'setInt8Import', kSig_v_rii);
  kDataViewSetInt16 =
      builder.addImport('DataView', 'setInt16Import', kSig_v_riii);
  kDataViewSetInt32 =
      builder.addImport('DataView', 'setInt32Import', kSig_v_riii);
  kDataViewSetUint8 =
      builder.addImport('DataView', 'setUint8Import', kSig_v_rii);
  kDataViewSetUint16 =
      builder.addImport('DataView', 'setUint16Import', kSig_v_riii);
  kDataViewSetUint32 =
      builder.addImport('DataView', 'setUint32Import', kSig_v_riii);

  return builder;
}

let kImports = {
  DataView: {
    getBigInt64Import:
        Function.prototype.call.bind(DataView.prototype.getBigInt64),
    getBigUint64Import:
        Function.prototype.call.bind(DataView.prototype.getBigUint64),
    getFloat32Import:
        Function.prototype.call.bind(DataView.prototype.getFloat32),
    getFloat64Import:
        Function.prototype.call.bind(DataView.prototype.getFloat64),
    getInt8Import: Function.prototype.call.bind(DataView.prototype.getInt8),
    getInt16Import: Function.prototype.call.bind(DataView.prototype.getInt16),
    getInt32Import: Function.prototype.call.bind(DataView.prototype.getInt32),
    getUint8Import: Function.prototype.call.bind(DataView.prototype.getUint8),
    getUint16Import: Function.prototype.call.bind(DataView.prototype.getUint16),
    getUint32Import: Function.prototype.call.bind(DataView.prototype.getUint32),
    setBigInt64Import:
        Function.prototype.call.bind(DataView.prototype.setBigInt64),
    setBigUint64Import:
        Function.prototype.call.bind(DataView.prototype.setBigUint64),
    setFloat32Import:
        Function.prototype.call.bind(DataView.prototype.setFloat32),
    setFloat64Import:
        Function.prototype.call.bind(DataView.prototype.setFloat64),
    setInt8Import: Function.prototype.call.bind(DataView.prototype.setInt8),
    setInt16Import: Function.prototype.call.bind(DataView.prototype.setInt16),
    setInt32Import: Function.prototype.call.bind(DataView.prototype.setInt32),
    setUint8Import: Function.prototype.call.bind(DataView.prototype.setUint8),
    setUint16Import: Function.prototype.call.bind(DataView.prototype.setUint16),
    setUint32Import: Function.prototype.call.bind(DataView.prototype.setUint32),
  },
};

function CheckStackTrace(thrower, reference, topmost_wasm_func) {
  let reference_exception;
  let actual_exception;
  try {
    thrower();
    assertUnreachable();
  } catch (e) {
    actual_exception = e;
  }
  try {
    reference();
    assertUnreachable();
  } catch (e) {
    reference_exception = e;
  }
  assertInstanceof(actual_exception, reference_exception.constructor);
  let actual_stack = actual_exception.stack.split('\n');
  let reference_stack = reference_exception.stack.split('\n');
  assertEquals(reference_stack[0], actual_stack[0]);
  assertEquals(reference_stack[1], actual_stack[1]);

  let str_stack_msg = `    at ${topmost_wasm_func} (wasm://wasm/`;
  if (!actual_stack[2].startsWith(str_stack_msg)) {
    console.log(
        `expected starts with:\n${str_stack_msg}\nfound:\n${actual_stack[2]}`);
    assertUnreachable();
  }
}

(function TestGetBigInt64() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('getBigInt64', kSig_l_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewGetBigInt64,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new BigInt64Array(2);
  array[0] = 0x7FFFFFFFFFFFFFFFn;
  array[1] = 0x12345678n;

  let dataview = new DataView(array.buffer);

  assertEquals(
      0x7FFFFFFFFFFFFFFFn, instance.exports.getBigInt64(dataview, 0, 1));
  assertEquals(0x12345678n, instance.exports.getBigInt64(dataview, 8, 1));
  assertEquals(
      0x7856341200000000n, instance.exports.getBigInt64(dataview, 8, 0));

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.getBigInt64('test_string', 0, 1),
      () => DataView.prototype.getBigInt64.call('test_string', 0, 1),
      'getBigInt64');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.getBigInt64(dataview, -1, 1),
      () => DataView.prototype.getBigInt64.call(dataview, -1, 1),
      'getBigInt64');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.getBigInt64(dataview, 15, 1),
      () => DataView.prototype.getBigInt64.call(dataview, 15, 1),
      'getBigInt64');
  CheckStackTrace(
      () => instance.exports.getBigInt64(dataview, 16, 1),
      () => DataView.prototype.getBigInt64.call(dataview, 16, 1),
      'getBigInt64');
  CheckStackTrace(
      () => instance.exports.getBigInt64(dataview, 17, 1),
      () => DataView.prototype.getBigInt64.call(dataview, 17, 1),
      'getBigInt64');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.getBigInt64(dataview, 0, 1),
      () => DataView.prototype.getBigInt64.call(dataview, 0, 1), 'getBigInt64');
})();

(function TestGetBigUint64() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('getBigUint64', kSig_l_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewGetBigUint64,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new BigUint64Array(2);
  array[0] = 0x7FFFFFFFFFFFFFFFn;
  array[1] = 0x12345678n;

  let dataview = new DataView(array.buffer);

  assertEquals(
      0x7FFFFFFFFFFFFFFFn, instance.exports.getBigUint64(dataview, 0, 1));
  assertEquals(
      0x7856341200000000n, instance.exports.getBigUint64(dataview, 8, 0));

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.getBigUint64('test_string', 0, 1),
      () => DataView.prototype.getBigUint64.call('test_string', 0, 1),
      'getBigUint64');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.getBigUint64(dataview, -1, 1),
      () => DataView.prototype.getBigUint64.call(dataview, -1, 1),
      'getBigUint64');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.getBigUint64(dataview, 16, 1),
      () => DataView.prototype.getBigUint64.call(dataview, 16, 1),
      'getBigUint64');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.getBigUint64(dataview, 0, 1),
      () => DataView.prototype.getBigUint64.call(dataview, 0, 1),
      'getBigUint64');
})();

(function TestGetFloat32() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('getFloat32', kSig_f_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewGetFloat32,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Float32Array(2);
  array[0] = 140.125;
  array[1] = -2048.015625;

  let dataview = new DataView(array.buffer);

  assertEquals(140.125, instance.exports.getFloat32(dataview, 0, 1));
  assertEquals(
      dataview.getFloat32(4, 0), instance.exports.getFloat32(dataview, 4, 0));

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.getFloat32('test_string', 0, 1),
      () => DataView.prototype.getFloat32.call('test_string', 0, 1),
      'getFloat32');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.getFloat32(dataview, -1, 1),
      () => DataView.prototype.getFloat32.call(dataview, -1, 1), 'getFloat32');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.getFloat32(dataview, 8, 1),
      () => DataView.prototype.getFloat32.call(dataview, 8, 1), 'getFloat32');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.getFloat32(dataview, 0, 1),
      () => DataView.prototype.getFloat32.call(dataview, 0, 1), 'getFloat32');
})();

(function TestGetFloat64() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('getFloat64', kSig_d_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewGetFloat64,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Float64Array(2);
  array[0] = 140.125;
  array[1] = -20480000.001953125;

  let dataview = new DataView(array.buffer);

  assertEquals(140.125, instance.exports.getFloat64(dataview, 0, 1));
  assertEquals(
      dataview.getFloat64(8, 0), instance.exports.getFloat64(dataview, 8, 0));

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.getFloat64('test_string', 0, 1),
      () => DataView.prototype.getFloat64.call('test_string', 0, 1),
      'getFloat64');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.getFloat64(dataview, -1, 1),
      () => DataView.prototype.getFloat64.call(dataview, -1, 1), 'getFloat64');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.getFloat64(dataview, 16, 1),
      () => DataView.prototype.getFloat64.call(dataview, 16, 1), 'getFloat64');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.getFloat64(dataview, 0, 1),
      () => DataView.prototype.getFloat64.call(dataview, 0, 1), 'getFloat64');
})();

(function TestGetInt8() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('getInt8', kSig_i_ri).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprCallFunction, kDataViewGetInt8,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Int8Array(2);
  array[0] = 127;
  array[1] = -64;

  let dataview = new DataView(array.buffer);

  assertEquals(127, instance.exports.getInt8(dataview, 0));
  assertEquals(-64, instance.exports.getInt8(dataview, 1));

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.getInt8('test_string', 0),
      () => DataView.prototype.getInt8.call('test_string', 0), 'getInt8');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.getInt8(dataview, -1),
      () => DataView.prototype.getInt8.call(dataview, -1), 'getInt8');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.getInt8(dataview, 2),
      () => DataView.prototype.getInt8.call(dataview, 2), 'getInt8');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.getInt8(dataview, 0),
      () => DataView.prototype.getInt8.call(dataview, 0), 'getInt8');
})();

(function TestGetInt16() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('getInt16', kSig_i_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewGetInt16,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Int16Array(2);
  array[0] = 32767;
  array[1] = 0x1234;

  let dataview = new DataView(array.buffer);

  assertEquals(32767, instance.exports.getInt16(dataview, 0, 1));
  assertEquals(0x3412, instance.exports.getInt16(dataview, 2, 0));

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.getInt16('test_string', 0, 1, 1),
      () => DataView.prototype.getInt16.call('test_string', 0, 1), 'getInt16');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.getInt16(dataview, -1, 1),
      () => DataView.prototype.getInt16.call(dataview, -1, 1), 'getInt16');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.getInt16(dataview, 4, 1),
      () => DataView.prototype.getInt16.call(dataview, 4, 1), 'getInt16');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.getInt16(dataview, 0, 1),
      () => DataView.prototype.getInt16.call(dataview, 0, 1), 'getInt16');
})();

(function TestGetInt32() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('getInt32', kSig_i_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewGetInt32,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Int32Array(2);
  array[0] = 42;
  array[1] = 0x12345678;

  let dataview = new DataView(array.buffer);

  assertEquals(42, instance.exports.getInt32(dataview, 0, 1));
  assertEquals(0x12345678, instance.exports.getInt32(dataview, 4, 1));
  assertEquals(0x78563412, instance.exports.getInt32(dataview, 4, 0));

  // Incompatible receiver.
  assertThrows(
      () => {instance.exports.getInt32('test_string', 0, 1)}, TypeError);
  CheckStackTrace(
      () => instance.exports.getInt32('test_string', 0, 1),
      () => DataView.prototype.getInt32.call('test_string', 0, 1), 'getInt32');

  // Offset bounds check.
  assertThrows(() => {instance.exports.getInt32(dataview, -1, 1)}, RangeError);
  CheckStackTrace(
      () => instance.exports.getInt32(dataview, -1, 1),
      () => DataView.prototype.getInt32.call(dataview, -1, 1), 'getInt32');

  // Dataview bounds check.
  assertThrows(() => {instance.exports.getInt32(dataview, 8, 1)}, RangeError);
  CheckStackTrace(
      () => instance.exports.getInt32(dataview, 7, 1),
      () => DataView.prototype.getInt32.call(dataview, 7, 1), 'getInt32');
  CheckStackTrace(
      () => instance.exports.getInt32(dataview, 8, 1),
      () => DataView.prototype.getInt32.call(dataview, 8, 1), 'getInt32');
  CheckStackTrace(
      () => instance.exports.getInt32(dataview, 0x7FFF_FFFF, 1),
      () => DataView.prototype.getInt32.call(dataview, 0x7FFF_FFFF, 1),
      'getInt32');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  assertThrows(() => {instance.exports.getInt32(dataview, 0, 1)}, TypeError);
  CheckStackTrace(
      () => instance.exports.getInt32(dataview, 0, 1),
      () => DataView.prototype.getInt32.call(dataview, 0, 1), 'getInt32');
})();

(function TestGetUint8() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('getUint8', kSig_i_ri).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprCallFunction, kDataViewGetUint8,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Uint8Array(1);
  array[0] = 255;

  let dataview = new DataView(array.buffer);

  assertEquals(255, instance.exports.getUint8(dataview, 0));

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.getUint8('test_string', 0),
      () => DataView.prototype.getUint8.call('test_string', 0), 'getUint8');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.getUint8(dataview, -1),
      () => DataView.prototype.getUint8.call(dataview, -1), 'getUint8');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.getUint8(dataview, 1),
      () => DataView.prototype.getUint8.call(dataview, 1), 'getUint8');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.getUint8(dataview, 0),
      () => DataView.prototype.getUint8.call(dataview, 0), 'getUint8');
})();

(function TestGetUint16() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('getUint16', kSig_i_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewGetUint16,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Uint16Array(2);
  array[0] = 0xFFFF;
  array[1] = 0x1234;

  let dataview = new DataView(array.buffer);

  assertEquals(0xFFFF, instance.exports.getUint16(dataview, 0, 1));
  assertEquals(0x3412, instance.exports.getUint16(dataview, 2, 0));

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.getUint16('test_string', 0, 1),
      () => DataView.prototype.getUint16.call('test_string', 0, 1),
      'getUint16');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.getUint16(dataview, -1, 1),
      () => DataView.prototype.getUint16.call(dataview, -1, 1), 'getUint16');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.getUint16(dataview, 4, 1),
      () => DataView.prototype.getUint16.call(dataview, 4, 1), 'getUint16');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.getUint16(dataview, 0, 1),
      () => DataView.prototype.getUint16.call(dataview, 0, 1), 'getUint16');
})();

(function TestGetUint32() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('getUint32', kSig_i_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewGetUint32,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Uint32Array(2);
  array[0] = 0x7FFFFFFF;
  array[1] = 0x12345678;

  let dataview = new DataView(array.buffer);

  assertEquals(0x7FFFFFFF, instance.exports.getUint32(dataview, 0, 1));
  assertEquals(0x78563412, instance.exports.getUint32(dataview, 4, 0));

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.getUint32('test_string', 0, 1),
      () => DataView.prototype.getUint32.call('test_string', 0, 1),
      'getUint32');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.getUint32(dataview, -1, 1),
      () => DataView.prototype.getUint32.call(dataview, -1, 1), 'getUint32');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.getUint32(dataview, 8, 1),
      () => DataView.prototype.getUint32.call(dataview, 8, 1), 'getUint32');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.getUint32(dataview, 0, 1),
      () => DataView.prototype.getUint32.call(dataview, 0, 1), 'getUint32');
})();

(function TestSetBigInt64() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('setBigInt64', kSig_v_rili).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprCallFunction, kDataViewSetBigInt64,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new BigInt64Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setBigInt64(dataview, 0, 0x7FFFFFFFFFFFFFFFn, 1);
  assertEquals(0x7FFFFFFFFFFFFFFFn, array[0]);
  instance.exports.setBigInt64(dataview, 0, -0x7FFFFFFFFFFFFFFFn, 1);
  assertEquals(-0x7FFFFFFFFFFFFFFFn, array[0]);
  instance.exports.setBigInt64(dataview, 8, 0x12345678n, 0);
  assertEquals(0x7856341200000000n, array[1]);

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.setBigInt64('test_string', 0, 50n, 1),
      () => DataView.prototype.setBigInt64.call('test_string', 0, 50n, 1),
      'setBigInt64');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.setBigInt64(dataview, -1, 50n, 1),
      () => DataView.prototype.setBigInt64.call(dataview, -1, 50n, 1),
      'setBigInt64');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.setBigInt64(dataview, 16, 50n, 1),
      () => DataView.prototype.setBigInt64.call(dataview, 16, 50n, 1),
      'setBigInt64');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.setBigInt64(dataview, 0, 50n, 1),
      () => DataView.prototype.setBigInt64.call(dataview, 0, 50n, 1),
      'setBigInt64');
})();

(function TestSetBigUint64() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('setBigUint64', kSig_v_rili).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprCallFunction, kDataViewSetBigUint64,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new BigUint64Array(1);
  let dataview = new DataView(array.buffer);

  instance.exports.setBigUint64(dataview, 0, 0xFFFFFFFFFFFFFFFFn, 1);
  assertEquals(0xFFFFFFFFFFFFFFFFn, array[0]);
  instance.exports.setBigUint64(dataview, 0, 0x12345678n, 0);
  assertEquals(0x7856341200000000n, array[0]);

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.setBigUint64('test_string', 0, 50n, 1),
      () => DataView.prototype.setBigUint64.call('test_string', 0, 50n, 1),
      'setBigUint64');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.setBigUint64(dataview, -1, 50n, 1),
      () => DataView.prototype.setBigUint64.call(dataview, -1, 50n, 1),
      'setBigUint64');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.setBigUint64(dataview, 16, 50n, 1),
      () => DataView.prototype.setBigUint64.call(dataview, 16, 50n, 1),
      'setBigUint64');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.setBigUint64(dataview, 0, 50n, 1),
      () => DataView.prototype.setBigUint64.call(dataview, 0, 50n, 1),
      'setBigUint64');
})();

(function TestSetFloat32() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('setFloat32', kSig_v_rifi).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprCallFunction, kDataViewSetFloat32,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Float32Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setFloat32(dataview, 0, 50.5, 1);
  assertEquals(50.5, array[0]);
  instance.exports.setFloat32(dataview, 0, 0x1234, 0);
  dataview.setFloat32(4, 0x1234, 0);
  assertEquals(array[1], array[0]);

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.setFloat32('test_string', 0, 100.55, 1),
      () => DataView.prototype.setFloat32.call('test_string', 0, 100.55, 1),
      'setFloat32');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.setFloat32(dataview, -1, 100.5, 1),
      () => DataView.prototype.setFloat32.call(dataview, -1, 100.5, 1),
      'setFloat32');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.setFloat32(dataview, 8, 100, 1),
      () => DataView.prototype.setFloat32.call(dataview, 8, 100, 1),
      'setFloat32');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.setFloat32(dataview, 0, 100, 1),
      () => DataView.prototype.setFloat32.call(dataview, 0, 100, 1),
      'setFloat32');
})();

(function TestSetFloat64() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('setFloat64', kSig_v_ridi).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprCallFunction, kDataViewSetFloat64,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Float64Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setFloat64(dataview, 0, 42.5, 1);
  assertEquals(42.5, array[0]);
  instance.exports.setFloat64(dataview, 0, 0x1234, 0);
  dataview.setFloat64(8, 0x1234, 0);
  assertEquals(array[1], array[0]);

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.setFloat64('test_string', 0, 100.55, 1),
      () => DataView.prototype.setFloat64.call('test_string', 0, 100.55, 1),
      'setFloat64');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.setFloat64(dataview, -1, 100.5, 1),
      () => DataView.prototype.setFloat64.call(dataview, -1, 100.5, 1),
      'setFloat64');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.setFloat64(dataview, 16, 100, 1),
      () => DataView.prototype.setFloat64.call(dataview, 16, 100, 1),
      'setFloat64');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.setFloat64(dataview, 0, 100, 1),
      () => DataView.prototype.setFloat64.call(dataview, 0, 100, 1),
      'setFloat64');
})();

(function TestSetInt8() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('setInt8', kSig_v_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewSetInt8,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Int8Array(1);
  let dataview = new DataView(array.buffer);

  instance.exports.setInt8(dataview, 0, 127);
  assertEquals(127, array[0]);

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.setInt8('test_string', 0, 100),
      () => DataView.prototype.setInt8.call('test_string', 0, 100),
      'setInt8');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.setInt8(dataview, -1, 100),
      () => DataView.prototype.setInt8.call(dataview, -1, 100), 'setInt8');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.setInt8(dataview, 1, 100),
      () => DataView.prototype.setInt8.call(dataview, 1, 100), 'setInt8');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.setInt8(dataview, 0, 100),
      () => DataView.prototype.setInt8.call(dataview, 0, 100), 'setInt8');
})();

(function TestSetInt16() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('setInt16', kSig_v_riii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprCallFunction, kDataViewSetInt16,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Int16Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setInt16(dataview, 0, 0x1234, 1);
  assertEquals(0x1234, array[0]);
  instance.exports.setInt16(dataview, 2, 0x1234, 0);
  assertEquals(0x3412, array[1]);

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.setInt16('test_string', 0, 100),
      () => DataView.prototype.setInt16.call('test_string', 0, 100), 'setInt16');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.setInt16(dataview, -1, 100),
      () => DataView.prototype.setInt16.call(dataview, -1, 100), 'setInt16');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.setInt16(dataview, 4, 100),
      () => DataView.prototype.setInt16.call(dataview, 4, 100), 'setInt16');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.setInt16(dataview, 0, 100),
      () => DataView.prototype.setInt16.call(dataview, 0, 100), 'setInt16');
})();

(function TestSetInt32() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('setInt32', kSig_v_riii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprCallFunction, kDataViewSetInt32,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Int32Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setInt32(dataview, 0, 50, 1);
  assertEquals(50, array[0]);
  instance.exports.setInt32(dataview, 4, 100, 1);
  assertEquals(100, array[1]);
  instance.exports.setInt32(dataview, 0, 0x12345678, 0);
  assertEquals(0x78563412, array[0]);

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.setInt32('test_string', 0, 100, 1),
      () => DataView.prototype.setInt32.call('test_string', 0, 100, 1),
      'setInt32');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.setInt32(dataview, -1, 100, 1),
      () => DataView.prototype.setInt32.call(dataview, -1, 100, 1), 'setInt32');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.setInt32(dataview, 7, 100, 1),
      () => DataView.prototype.setInt32.call(dataview, 7, 100, 1), 'setInt32');
  CheckStackTrace(
      () => instance.exports.setInt32(dataview, 8, 100, 1),
      () => DataView.prototype.setInt32.call(dataview, 8, 100, 1), 'setInt32');
  CheckStackTrace(
      () => instance.exports.setInt32(dataview, 9, 100, 1),
      () => DataView.prototype.setInt32.call(dataview, 9, 100, 1), 'setInt32');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.setInt32(dataview, 0, 100, 1),
      () => DataView.prototype.setInt32.call(dataview, 0, 100, 1), 'setInt32');
})();

(function TestSetUint8() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('setUint8', kSig_v_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewSetUint8,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Uint8Array(1);
  let dataview = new DataView(array.buffer);

  instance.exports.setUint8(dataview, 0, 255);
  assertEquals(255, array[0]);

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.setUint8('test_string', 0, 100),
      () => DataView.prototype.setUint8.call('test_string', 0, 100),
      'setUint8');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.setUint8(dataview, -1, 100),
      () => DataView.prototype.setUint8.call(dataview, -1, 100), 'setUint8');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.setUint8(dataview, 1, 100),
      () => DataView.prototype.setUint8.call(dataview, 1, 100), 'setUint8');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.setUint8(dataview, 0, 100),
      () => DataView.prototype.setUint8.call(dataview, 0, 100), 'setUint8');
})();

(function TestSetUint16() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('setUint16', kSig_v_riii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprCallFunction, kDataViewSetUint16,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Uint16Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setUint16(dataview, 0, 0x1234, 1);
  assertEquals(0x1234, array[0]);
  instance.exports.setUint16(dataview, 2, 0x1234, 0);
  assertEquals(0x3412, array[1]);

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.setUint16('test_string', 0, 100),
      () => DataView.prototype.setUint16.call('test_string', 0, 100),
      'setUint16');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.setUint16(dataview, -1, 100),
      () => DataView.prototype.setUint16.call(dataview, -1, 100), 'setUint16');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.setUint16(dataview, 4, 100),
      () => DataView.prototype.setUint16.call(dataview, 4, 100), 'setUint16');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.setUint16(dataview, 0, 100),
      () => DataView.prototype.setUint16.call(dataview, 0, 100), 'setUint16');
})();

(function TestSetUint32() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction('setUint32', kSig_v_riii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprCallFunction, kDataViewSetUint32,
  ]);
  let instance = builder.instantiate(kImports);
  let array = new Uint32Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setUint32(dataview, 0, 50, 1);
  assertEquals(50, array[0]);
  instance.exports.setUint32(dataview, 4, 0x12345678, 0);
  assertEquals(0x78563412, array[1]);

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.setUint32('test_string', 0, 100, 1),
      () => DataView.prototype.setUint32.call('test_string', 0, 100, 1),
      'setUint32');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.setUint32(dataview, -1, 100, 1),
      () => DataView.prototype.setUint32.call(dataview, -1, 100, 1),
      'setUint32');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.setUint32(dataview, 8, 100, 1),
      () => DataView.prototype.setUint32.call(dataview, 8, 100, 1),
      'setUint32');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.setUint32(dataview, 0, 100, 1),
      () => DataView.prototype.setUint32.call(dataview, 0, 100, 1),
      'setUint32');
})();
