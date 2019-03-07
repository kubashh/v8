// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

load("test/mjsunit/wasm/wasm-module-builder.js");

// This is based on WebAssembly's spec limits tests

const kJSEmbeddingMaxFunctionLocals = 50000;
const kJSEmbeddingMaxFunctionParams = 1000;
const kJSEmbeddingMaxFunctionReturns = 1;
const kJSEmbeddingMaxTableSize = 10000000;

function testLimit(name, limit, gen) {
  function get_buffer(count) {
    const builder = new WasmModuleBuilder();
    gen(builder, count);
    return builder;
  }

  const builder_with_limit = get_buffer(limit);
  const builder_with_limit_plus_1 = get_buffer(limit + 1);

  print(`Test${name}AtLimit`);
  builder_with_limit.instantiate({});

  print(`Test${name}AboveLimit`);
  assertThrows(() => builder_with_limit_plus_1.instantiate({}),
               WebAssembly.CompileError);
}

testLimit("function locals", kJSEmbeddingMaxFunctionLocals, (builder, count) => {
        const type = builder.addType(kSig_v_v);
        builder.addFunction(undefined, type)
          .addLocals({i32_count: count})
          .addBody([]);
    });

testLimit("function params", kJSEmbeddingMaxFunctionParams, (builder, count) => {
        const array = new Array(count);
        for (let i = 0; i < count; i++) {
            array[i] = kWasmI32;
        }
        const type = builder.addType({params: array, results: []});
    });

testLimit("function params+locals", kJSEmbeddingMaxFunctionLocals - 2, (builder, count) => {
        const type = builder.addType(kSig_i_ii);
        builder.addFunction(undefined, type)
          .addLocals({i32_count: count})
          .addBody([kExprUnreachable]);
    });

testLimit("function returns", kJSEmbeddingMaxFunctionReturns, (builder, count) => {
        const array = new Array(count);
        for (let i = 0; i < count; i++) {
            array[i] = kWasmI32;
        }
        const type = builder.addType({params: [], results: array});
    });

testLimit("initial table size", kJSEmbeddingMaxTableSize, (builder, count) => {
        builder.setTableBounds(count, undefined);
    });

testLimit("maximum table size", kJSEmbeddingMaxTableSize, (builder, count) => {
        builder.setTableBounds(1, count);
    });
