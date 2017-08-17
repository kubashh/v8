// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --stress-compaction

load('test/mjsunit/wasm/wasm-constants.js');
load('test/mjsunit/wasm/wasm-module-builder.js');

var initialMemoryPages = 1;
var maximumMemoryPages = 9;

function generateBuilder() {
  let builder = new WasmModuleBuilder();
  builder.addMemory(initialMemoryPages, maximumMemoryPages, true);
  builder.addFunction('store', kSig_i_ii)
      .addBody([
        kExprGetLocal, 0, kExprGetLocal, 1, kExprI32StoreMem, 0, 0,
        kExprGetLocal, 1
      ])
      .exportFunc();
  return builder;
}

// This test verifies that the effects of growing memory inside a loop
// affect the result of current_memory when the loop is over.
(function TestGrowMemoryInsideLoop() {
  print('TestGrowMemoryInsideLoop ...');
  let deltaPages = 2;
  let builder = generateBuilder();
  builder.addFunction('main', kSig_i_i)
      .addBody([
        // clang-format off
        kExprLoop, kWasmStmt,                   // while
          kExprGetLocal, 0,                     // -
          kExprIf, kWasmStmt,                   // if <param0> != 0
            // Grow memory.
            kExprI32Const, deltaPages,          // -
            kExprGrowMemory, kMemoryZero,       // grow memory
            kExprDrop,                          // drop the result of grow
            // Decrease loop variable.
            kExprGetLocal, 0,                   // -
            kExprI32Const, 1,                   // -
            kExprI32Sub,                        // -
            kExprSetLocal, 0,                   // decrease <param0>
            kExprBr, 1,                         // continue
          kExprEnd,                             // -
        kExprEnd,                               // break
        // Return the memory size.
        kExprMemorySize, kMemoryZero            // put memory size on stack
        // clang-format on
      ])
      .exportFunc();
  // Avoid the loop branch (not growing memory).
  var instance = builder.instantiate();
  var iterations = 0;
  var expectedMemoryPages = initialMemoryPages + iterations * deltaPages;
  assertTrue(expectedMemoryPages <= maximumMemoryPages);
  assertEquals(expectedMemoryPages, instance.exports.main(iterations));
  // Enter the loop branch (growing memory).
  var instance = builder.instantiate();
  var iterations = 2;
  var expectedMemoryPages = initialMemoryPages + iterations * deltaPages;
  assertTrue(expectedMemoryPages <= maximumMemoryPages);
  assertEquals(expectedMemoryPages, instance.exports.main(iterations));
})();

// This test verifies that a loop does not affect the result of current_memory
// when the memory is grown both inside and outside the loop.
(function TestGrowMemoryInsideAndOutsideLoop() {
  print('TestGrowMemoryInsideAndOutsideLoop ...');
  let deltaPages = 2;
  let builder = generateBuilder();
  builder.addFunction('main', kSig_i_i)
      .addBody([
        // clang-format off
        // Grow memory.
        kExprI32Const, deltaPages,              // -
        kExprGrowMemory, kMemoryZero,           // grow memory
        kExprDrop,                              // drop the result of grow
        kExprLoop, kWasmStmt,                   // while
          kExprGetLocal, 0,                     // -
          kExprIf, kWasmStmt,                   // if <param0> != 0
            // Grow memory.
            kExprI32Const, deltaPages,          // -
            kExprGrowMemory, kMemoryZero,       // grow memory
            kExprDrop,                          // drop the result of grow
            // Decrease loop variable.
            kExprGetLocal, 0,                   // -
            kExprI32Const, 1,                   // -
            kExprI32Sub,                        // -
            kExprSetLocal, 0,                   // decrease <param0>
            kExprBr, 1,                         // continue
          kExprEnd,                             // -
        kExprEnd,                               // break
        // Grow memory.
        kExprI32Const, deltaPages,              // -
        kExprGrowMemory, kMemoryZero,           // grow memory
        kExprDrop,                              // drop the result of grow
        // Return memory size.
        kExprMemorySize, kMemoryZero            // put memory size on stack
        // clang-format on
      ])
      .exportFunc();
  // Avoid the loop branch (grow memory by 2 * deltaPages).
  var instance = builder.instantiate();
  var iterations = 0;
  var expectedMemoryPages = initialMemoryPages + (2 + iterations) * deltaPages;
  assertTrue(expectedMemoryPages <= maximumMemoryPages);
  assertEquals(expectedMemoryPages, instance.exports.main(iterations));
  // Enter the loop branch (grow memory by (2 + iterations) * deltaPages).
  var instance = builder.instantiate();
  var iterations = 2;
  var expectedMemoryPages = initialMemoryPages + (2 + iterations) * deltaPages;
  assertTrue(expectedMemoryPages <= maximumMemoryPages);
  assertEquals(expectedMemoryPages, instance.exports.main(iterations));
})();

// This test verifies that the effects of writing to memory grown inside a loop
// are retained when the loop is over.
(function TestGrowMemoryAndStoreInsideLoop() {
  print('TestGrowMemoryAndStoreInsideLoop ...');
  let index = 0;
  let initial = 1;
  let deltaPages = 1;
  let builder = generateBuilder();
  builder.addFunction('main', kSig_i_i)
      .addBody([
        // clang-format off
        kExprLoop, kWasmStmt,                   // while
          kExprGetLocal, 0,                     // -
          kExprIf, kWasmStmt,                   // if <param0> != 0
            // Grow memory.
            kExprI32Const, deltaPages,          // -
            kExprGrowMemory, kMemoryZero,       // grow memory
            kExprDrop,                          // drop the result of grow
            // Increase counter in memory.
            kExprI32Const, index,               // put index (for store)
            kExprI32Const, index,               // put index (for load)
            kExprI32LoadMem, 0, 0,              // load from grown memory
            kExprI32Const, 1,                   // -
            kExprI32Add,                        // increase counter
            kExprI32StoreMem, 0, 0,             // store counter in memory
            // Decrease loop variable.
            kExprGetLocal, 0,                   // -
            kExprI32Const, 1,                   // -
            kExprI32Sub,                        // -
            kExprSetLocal, 0,                   // decrease <param0>
            kExprBr, 1,                         // continue
          kExprEnd,                             // -
        kExprEnd,                               // break
        // Increase counter in memory.
        kExprI32Const, index,                   // -
        kExprI32LoadMem, 0, 0                   // load from grown memory
        // clang-format on
      ])
      .exportFunc();
  // Avoid the loop (not growing memory).
  var instance = builder.instantiate();
  var iterations = 0;
  instance.exports.store(index, initial);
  assertEquals(initial, instance.exports.main(iterations));
  // Enter the loop (growing memory + storing newValue in grown memory).
  var instance = builder.instantiate();
  var iterations = 2;
  instance.exports.store(index, initial);
  assertEquals(initial + iterations, instance.exports.main(iterations));
})();

// This test verifies that a loop does not affect the memory when the
// memory is grown both inside and outside the loop.
(function TestGrowMemoryAndStoreInsideAndOutsideLoop() {
  print('TestGrowMemoryAndStoreInsideAndOutsideLoop ...');
  let index = 0;
  let initial = 1;
  let deltaPages = 2;
  let builder = generateBuilder();
  builder.addFunction('main', kSig_i_i)
      .addBody([
        // clang-format off
        // Grow memory.
        kExprI32Const, deltaPages,              // put <deltaPages> on stack
        kExprGrowMemory, kMemoryZero,           // grow memory
        kExprDrop,                              // drop the result of grow
        // Increase counter in memory.
        kExprI32Const, index,                   // put index (for store)
        kExprI32Const, index,                   // put index (for load)
        kExprI32LoadMem, 0, 0,                  // load from grown memory
        kExprI32Const, 1,                       // -
        kExprI32Add,                            // increase value on stack
        kExprI32StoreMem, 0, 0,                 // store new value
        // Start loop.
        kExprLoop, kWasmStmt,                   // while
          kExprGetLocal, 0,                     // -
          kExprIf, kWasmStmt,                   // if <param0> != 0
            // Grow memory.
            kExprI32Const, deltaPages,          // put <deltaPages> on stack
            kExprGrowMemory, kMemoryZero,       // grow memory
            kExprDrop,                          // drop the result of grow
            // Increase counter in memory.
            kExprI32Const, index,               // put index (for store)
            kExprI32Const, index,               // put index (for load)
            kExprI32LoadMem, 0, 0,              // load from grown memory
            kExprI32Const, 1,                   // -
            kExprI32Add,                        // increase value on stack
            kExprI32StoreMem, 0, 0,             // store new value
            // Decrease loop variable.
            kExprGetLocal, 0,                   // -
            kExprI32Const, 1,                   // -
            kExprI32Sub,                        // -
            kExprSetLocal, 0,                   // decrease <param0>
            kExprBr, 1,                         // continue
          kExprEnd,                             // -
        kExprEnd,                               // break
        // Grow memory.
        kExprI32Const, deltaPages,              // put <deltaPages> on stack
        kExprGrowMemory, kMemoryZero,           // grow memory
        kExprDrop,                              // drop the result of grow
        // Increase counter in memory.
        kExprI32Const, index,                   // put index (for store)
        kExprI32Const, index,                   // put index (for load)
        kExprI32LoadMem, 0, 0,                  // load from grown memory
        kExprI32Const, 1,                       // -
        kExprI32Add,                            // increase value on stack
        kExprI32StoreMem, 0, 0,                 // store new value
        // Return counter from memory.
        kExprI32Const, index,                   // put the index on stack
        kExprI32LoadMem, 0, 0                   // load from grown memory
        // clang-format on
      ])
      .exportFunc();
  // Avoid the loop (not growing memory).
  var instance = builder.instantiate();
  var iterations = 0;
  instance.exports.store(index, initial);
  assertEquals(initial + 2, instance.exports.main(iterations));
  // Enter the loop (growing memory + storing newValue in grown memory).
  var instance = builder.instantiate();
  var iterations = 2;
  instance.exports.store(index, initial);
  assertEquals(initial + iterations + 2, instance.exports.main(iterations));
})();
