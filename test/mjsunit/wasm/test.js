// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
builder.addMemory64(1, 1);
builder.exportMemoryAs('memory');

builder.addFunction("dbl", kSig_v_v)
.addLocals(kWasmI32, 1)
.addBody([
  ...wasmI32Const(0),
  kExprLocalSet, 0,
  kExprLoop, kWasmVoid,
    kExprLocalGet, 0,  // index
    ...wasmI32Const(4096), // offset
    kExprF64LoadMem,
    ...wasmF64Const(2.0),
    kExprF64Mul,
    kExprLocalGet, 0,  // index
    ...wasmI32Const(4096), // offset
    kExprF64StoreMem,
    // if (index < 1024) goto loop;
    kExprLocalGet, 0, ...wasmI32Const(1024), kExprI32LtU,
    kExprBrIf, 0,
  kExprEnd])
.exportFunc();

let module = builder.instantiate();
let memory = module.exports.memory;

assertEquals(64 * 1024, memory.buffer.byteLength);
// Test that we can create a TypedArray from that large buffer.
let array = new Float64Array(memory.buffer);
assertEquals(num_bytes / 8, array.length);

for (let i = 0; i < array.length; i++) {
    array[i] = i;
}
dbl();
for (let i = 4096; i < 4096+128; i++) {
    print(array[i]);
}
