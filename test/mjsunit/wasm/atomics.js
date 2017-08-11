// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-threads

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

let kMemtypeSize32 = 4;
let kMemtypeSize16 = 2;
let kMemtypeSize8 = 1;

function Add(a, b) { return a + b; }
function Sub(a, b) { return a - b; }
function And(a, b) { return a & b; }
function Or(a, b) { return a | b; }
function Xor(a, b) { return a ^ b; }

let memory = new WebAssembly.Memory({initial: 1, maximum: 10, shared: true});
function GetAtomicBinOpFunction(WasmExpression, memory) {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "imported_mem");
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprGetLocal, 0,
      kExprGetLocal, 1,
      kAtomicPrefix,
      WasmExpression])
    .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = (new WebAssembly.Instance(module,
        {m: {imported_mem: memory}}));
  return instance.exports.main;
}

function VerifyBoundsCheck(buffer, func, memtype_size, max) {
  kPageSize = 65536;
  // Test out of bounds at boundary
  for (i = buffer.byteLength - memtype_size + 1;
       i < buffer.byteLength + memtype_size + 4; i++) {
    assertTraps(kTrapMemOutOfBounds, () => func(i, 5));
  }
  // Test out of bounds at maximum + 1
  assertTraps(kTrapMemOutOfBounds, () => func((max + 1) * kPageSize, 5));
}

function Test32Op(memory, operation, func) {
  let i32 = new Uint32Array(memory.buffer);
  for (i = i32.length - 1; i == 0; i--) {
    let expected = 0xacedf00d;
    let value = 0x11111111;
    i32[i] = expected;
    assertEquals(expected, func(i * kMemtypeSize32, value));
    assertEquals(operation(expected, value), i32[i]);
  }
  VerifyBoundsCheck(memory.buffer, func, kMemtypeSize32, 10);
}

function Test16Op(memory, operation, func) {
  let i16 = new Uint16Array(memory.buffer);
  for (i = 0; i < i16.length; i++) {
    let expected = 0xd00d;
    let value = 0x1111;
    i16[i] = expected;
    assertEquals(expected, func(i * kMemtypeSize16, value));
    assertEquals(operation(expected, value), i16[i]);
  }
  VerifyBoundsCheck(memory.buffer, func, kMemtypeSize16, 10);
}

function Test8Op(memory, operation, func) {
  let i8 = new Uint8Array(memory.buffer);
  for (i = 0; i < i8.length; i++) {
    let expected = 0xbe;
    let value = 0x12;
    i8[i] = expected;
    assertEquals(expected, func(i * kMemtypeSize8, value));
    assertEquals(operation(expected, value), i8[i]);
  }
  VerifyBoundsCheck(memory.buffer, func, kMemtypeSize8, 10);
}

(function TestAtomicAdd() {
  print("TestAtomicAdd");
  let wasmAdd = GetAtomicBinOpFunction(kExprI32AtomicAdd, memory);
  Test32Op(memory, Add, wasmAdd);
})();

(function TestAtomicAdd16U() {
  print("TestAtomicAdd16U");
  let wasmAdd = GetAtomicBinOpFunction(kExprI32AtomicAdd16U, memory);
  Test16Op(memory, Add, wasmAdd);
})();

(function TestAtomicAdd8U() {
  print("TestAtomicAdd8U");
  let wasmAdd = GetAtomicBinOpFunction(kExprI32AtomicAdd8U, memory);
  Test8Op(memory, Add, wasmAdd);
})();

(function TestAtomicSub() {
  print("TestAtomicSub");
  let wasmSub = GetAtomicBinOpFunction(kExprI32AtomicSub, memory);
  Test32Op(memory, Sub, wasmSub);
})();

(function TestAtomicSub16U() {
  print("TestAtomicSub16U");
  let wasmSub = GetAtomicBinOpFunction(kExprI32AtomicSub16U, memory);
  Test16Op(memory, Sub, wasmSub);
})();

(function TestAtomicSub8U() {
  print("TestAtomicSub8U");
  let wasmSub = GetAtomicBinOpFunction(kExprI32AtomicSub8U, memory);
  Test8Op(memory, Sub, wasmSub);
})();

(function TestAtomicAnd() {
  print("TestAtomicAnd");
  let wasmAnd = GetAtomicBinOpFunction(kExprI32AtomicAnd, memory);
  Test32Op(memory, And, wasmAnd);
})();

(function TestAtomicAnd16U() {
  print("TestAtomicAnd16U");
  let wasmAnd = GetAtomicBinOpFunction(kExprI32AtomicAnd16U, memory);
  Test16Op(memory, And, wasmAnd);
})();

(function TestAtomicAnd8U() {
  print("TestAtomicAnd8U");
  let wasmAnd = GetAtomicBinOpFunction(kExprI32AtomicAnd8U, memory);
  Test8Op(memory, And, wasmAnd);
})();

(function TestAtomicOr() {
  print("TestAtomicOr");
  let wasmOr = GetAtomicBinOpFunction(kExprI32AtomicOr, memory);
  Test32Op(memory, Or, wasmOr);
})();

(function TestAtomicOr16U() {
  print("TestAtomicOr16U");
  let wasmOr = GetAtomicBinOpFunction(kExprI32AtomicOr16U, memory);
  Test16Op(memory, Or, wasmOr);
})();

(function TestAtomicOr8U() {
  print("TestAtomicOr8U");
  let wasmOr = GetAtomicBinOpFunction(kExprI32AtomicOr8U, memory);
  Test8Op(memory, Or, wasmOr);
})();

(function TestAtomicXor() {
  print("TestAtomicXor");
  let wasmXor = GetAtomicBinOpFunction(kExprI32AtomicXor, memory);
  Test32Op(memory, Xor, wasmXor);
})();

(function TestAtomicXor16U() {
  print("TestAtomicXor16U");
  let wasmXor = GetAtomicBinOpFunction(kExprI32AtomicXor16U, memory);
  Test16Op(memory, Xor, wasmXor);
})();

(function TestAtomicXor8U() {
  print("TestAtomicXor8U");
  let wasmXor = GetAtomicBinOpFunction(kExprI32AtomicXor8U, memory);
  Test8Op(memory, Xor, wasmXor);
})();
