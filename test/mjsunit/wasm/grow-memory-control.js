// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --stress-compaction

load("test/mjsunit/wasm/wasm-constants.js");
load("test/mjsunit/wasm/wasm-module-builder.js");

function generateBuilder() {
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, 5, true);
  // The order of the functions here matters for invoking the correct function
  // in the tests below.
  builder.addFunction("grow", kSig_i_i)
    .addBody([kExprGetLocal, 0,
              kExprGrowMemory, kMemoryZero])
    .exportFunc();
  builder.addFunction("load", kSig_i_i)
    .addBody([kExprGetLocal, 0, kExprI32LoadMem, 0, 0])
    .exportFunc();
  builder.addFunction("store", kSig_i_ii)
    .addBody([kExprGetLocal, 0, kExprGetLocal, 1, kExprI32StoreMem, 0, 0,
              kExprGetLocal, 1])
    .exportFunc();
  return builder
}

/* Grow memory in directly called functions. */
print("=== grow_memory in direct calls ===");

// This test verifies that the current_memory instruction returns the correct
// value after returning from a function (direct call) that grew memory.
(function TestGrowMemoryInFunction() {
  print("TestGrowMemoryInFunction ...");
  let builder = generateBuilder();
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprGetLocal, 0,              // get number of new pages
              kExprCallFunction, 0,          // call the grow function
              kExprDrop,                     // drop the result of grow
              kExprMemorySize, kMemoryZero]) // get the memory size
    .exportFunc();
  var instance = builder.instantiate();
  // The caller should be aware that the memory was grown by the callee.
  assertEquals(2, instance.exports.main(1));
})();

// This test verifies that accessing a memory page that has been created inside
// a function (direct call) does not trap in the caller.
(function TestGrowMemoryAndAccessInFunction() {
  print("TestGrowMemoryAndAccessInFunction ...");
  let offset = 2*kPageSize - 4;
  let builder = generateBuilder();
  builder.addFunction("main", kSig_i_iii)
    .addBody([kExprGetLocal, 0,              // get number of new pages
              kExprGetLocal, 0,              // get index of the function
              kExprCallFunction, 0,          // call the grow function
              kExprDrop,                     // drop the result of grow
              kExprGetLocal, 1,              // get offset
              kExprGetLocal, 2,              // get value
              kExprI32StoreMem, 0, 0])       // store
    .exportFunc();
  var instance = builder.instantiate();
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.load(offset));
  instance.exports.main(1, offset, 1234);
  // The caller should be able to access memory that was grown by the callee.
  assertEquals(1234, instance.exports.load(offset));
})();

// This test verifies that when a function (direct call) grows and store
// something in the grown memory, the caller always read from the grown
// memory. This checks that the memory start address gets updated in the caller.
(function TestGrowMemoryAndStoreInFunction() {
  print("TestGrowMemoryAndStoreInFunction ...");
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, 5, true);
  builder.addFunction("grow_and_store", kSig_v_v)
    .addBody([kExprI32Const, 1,              // always grow memory by 1 page
              kExprGrowMemory, kMemoryZero,  // grow memory
              kExprDrop,                     // drop the result of grow
              kExprI32Const, 0,              // put offset on stack
              kExprI32Const, 42,             // put new value on stack
              kExprI32StoreMem, 0, 0])       // store
    .exportFunc();
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprI32Const, 0,              // put offset on stack
              kExprI32Const, 21,             // put old value on stack
              kExprI32StoreMem, 0, 0,        // store
              kExprCallFunction, 0,          // call grow_and_store
              kExprI32Const, 0,              // put offset on stack
              kExprI32LoadMem, 0, 0])        // load from grown memory
    .exportFunc();
  var instance = builder.instantiate();
  // The caller should always read from grown memory.
  assertEquals(42, instance.exports.main());
})();


/* Grow memory in indirectly called functions. */
print("\n=== grow_memory in indirect calls ===");

// This test verifies that the current_memory instruction returns the correct
// value after returning from a function (indirect call) that grew memory.
(function TestGrowMemoryInIndirectCall() {
  print("TestGrowMemoryInIndirectCall ...");
  let builder = generateBuilder();
  builder.addFunction("main", kSig_i_ii)
    .addBody([kExprGetLocal, 1,              // get number of new pages
              kExprGetLocal, 0,              // get index of the function
              kExprCallIndirect, 0, kTableZero,  // call the function
              kExprDrop,                     // drop the result of grow
              kExprMemorySize, kMemoryZero]) // get the memory size
    .exportFunc();
  builder.appendToTable([0]);
  var instance = builder.instantiate();
  // The caller should be aware that the memory was grown by the callee.
  assertEquals(2, instance.exports.main(0, 1));
})();

// This test verifies that accessing a memory page that has been created inside
// a function (indirect call) does not trap in the caller.
(function TestGrowMemoryAndAccessInIndirectCall() {
  print("TestGrowMemoryAndAccessInIndirectCall ...");
  let offset = 2*kPageSize - 4;
  let builder = generateBuilder();
  let sig = makeSig([kWasmI32, kWasmI32, kWasmI32, kWasmI32], []);
  builder.addFunction("main", sig)
    .addBody([kExprGetLocal, 1,              // get number of new pages
              kExprGetLocal, 0,              // get index of the function
              kExprCallIndirect, 0, kTableZero,  // call the function
              kExprDrop,                     // drop the result of grow
              kExprGetLocal, 2,              // get offset
              kExprGetLocal, 3,              // get value
              kExprI32StoreMem, 0, 0])       // store
    .exportFunc();
  builder.appendToTable([0]);
  var instance = builder.instantiate();
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.load(offset));
  instance.exports.main(0, 1, offset, 1234);
  // The caller should be able to access memory that was grown by the callee.
  assertEquals(1234, instance.exports.load(offset));
})();

// This test verifies that when a function (indirect call) grows and store
// something in the grown memory, the caller always read from the grown
// memory. This checks that the memory start address gets updated in the caller.
(function TestGrowMemoryAndStoreInIndirectCall() {
  print("TestGrowMemoryAndStoreInIndirectCall ...");
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, 5, true);
  builder.addFunction("grow_and_store", kSig_v_v)
    .addBody([kExprI32Const, 1,              // always grow memory by 1 page
              kExprGrowMemory, kMemoryZero,  // grow memory
              kExprDrop,                     // drop the result of grow
              kExprI32Const, 0,              // put offset on stack
              kExprI32Const, 42,             // put new value on stack
              kExprI32StoreMem, 0, 0])       // store
    .exportFunc();
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprI32Const, 0,              // put offset on stack
              kExprI32Const, 21,             // put old value on stack
              kExprI32StoreMem, 0, 0,        // store
              kExprGetLocal, 0,              // get index of the function
              kExprCallIndirect, 0, kTableZero,  // call the function
              kExprI32Const, 0,              // put offset on stack
              kExprI32LoadMem, 0, 0])        // load from grown memory
    .exportFunc();
  builder.appendToTable([0]);
  var instance = builder.instantiate();
  // The caller should always read from grown memory.
  assertEquals(42, instance.exports.main(0));
})();


/* Grow memory in conditional branches. */
print("\n=== grow_memory in conditional branches ===");

// This test verifies that the effects of growing memory in an if branch
// affect the result of current_memory when the branch is merged.
(function TestGrowMemoryInIfBranchNoElse() {
  print("TestGrowMemoryInIfBranchNoElse ...");
  let builder = generateBuilder();
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprGetLocal, 0,                // get condition parameter
              kExprI32Const, 1,
              kExprI32Eq,                      // compare it with 1
              kExprIf, kWasmStmt,              // if it's 1 then enter if
                kExprI32Const, 4,              // always grow by 4 pages
                kExprGrowMemory, kMemoryZero,  // grow memory
                kExprDrop,                     // drop the result of grow
              kExprEnd,
              kExprMemorySize, kMemoryZero])   // get the memory size
    .exportFunc();
  var instance = builder.instantiate();
  // Avoid the if branch (not growing memory).
  assertEquals(1, instance.exports.main(0));
  // Enter the if branch (growing memory).
  assertEquals(5, instance.exports.main(1));
})();

// This test verifies that the effects of growing memory in an if branch are
// retained when the branch is merged even when an else branch exists.
(function TestGrowMemoryInIfBranchWithElse() {
  print("TestGrowMemoryInIfBranchWithElse ...");
  let offset = 0;
  let old_value = 21;
  let new_value = 42;
  let builder = generateBuilder();
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprGetLocal, 0,                // get condition parameter
              kExprI32Const, 1,
              kExprI32Eq,                      // compare it with 1
              kExprIf, kWasmStmt,              // if it's 1 then enter if
                kExprI32Const, 4,              // always grow by 4 pages
                kExprGrowMemory, kMemoryZero,  // grow memory
                kExprDrop,                     // drop the result of grow
              kExprElse,
                kExprI32Const, offset,         // put offset on stack
                kExprI32Const, new_value,      // put the value on stack
                kExprI32StoreMem, 0, 0,        // store
              kExprEnd,
              kExprMemorySize, kMemoryZero])   // get the memory size
    .exportFunc();
  var instance = builder.instantiate();
  // Initialize the memory location with old_value.
  instance.exports.store(offset, old_value);
  assertEquals(old_value, instance.exports.load(offset));
  // Verify that the else branch (not growing) is reachable.
  assertEquals(1, instance.exports.main(0));
  assertEquals(new_value, instance.exports.load(offset));
  // Enter the if branch (growing memory).
  assertEquals(5, instance.exports.main(1));
})();

// This test verifies that the effects of growing memory in an else branch
// affect the result of current_memory when the branch is merged.
(function TestGrowMemoryInElseBranch() {
  print("TestGrowMemoryInElseBranch ...");
  let offset = 0;
  let old_value = 21;
  let new_value = 42;
  let builder = generateBuilder();
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprGetLocal, 0,                // get condition parameter
              kExprI32Const, 1,
              kExprI32Eq,                      // compare it with 1
              kExprIf, kWasmStmt,              // if it's 1 then enter if
                kExprI32Const, offset,         // put offset on stack
                kExprI32Const, new_value,      // put the value on stack
                kExprI32StoreMem, 0, 0,        // store
              kExprElse,
                kExprI32Const, 4,              // always grow by 4 pages
                kExprGrowMemory, kMemoryZero,  // grow memory
                kExprDrop,                     // drop the result of grow
              kExprEnd,
              kExprMemorySize, kMemoryZero])   // get the memory size
    .exportFunc();
  var instance = builder.instantiate();
  // Initialize the memory location with old_value.
  instance.exports.store(offset, old_value);
  assertEquals(old_value, instance.exports.load(offset));
  // Verify that the if branch (not growing) is reachable.
  assertEquals(1, instance.exports.main(1));
  assertEquals(new_value, instance.exports.load(offset));
  // Enter the else branch (growing memory).
  assertEquals(5, instance.exports.main(0));
})();

// This test verifies that the effects of growing memory in an if/else
// branch affect the result of current_memory when the branches are merged.
(function TestGrowMemoryInBothIfAndElse() {
  print("TestGrowMemoryInBothIfAndElse ...");
  let builder = generateBuilder();
  builder.addFunction("main", kSig_i_i)
    .addBody([kExprGetLocal, 0,                // get condition parameter
              kExprI32Const, 1,
              kExprI32Eq,                      // compare it with 1
              kExprIf, kWasmStmt,              // if it's 1 then enter if
                kExprI32Const, 1,              // always grow by 1 page
                kExprGrowMemory, kMemoryZero,  // grow memory
                kExprDrop,                     // drop the result of grow
              kExprElse,
                kExprI32Const, 2,              // always grow by 2 pages
                kExprGrowMemory, kMemoryZero,  // grow memory
                kExprDrop,                     // drop the result of grow
              kExprEnd,
              kExprMemorySize, kMemoryZero])   // get the memory size
    .exportFunc();
  var instance = builder.instantiate();
  // Enter the if branch (growing memory by 1 page).
  assertEquals(2, instance.exports.main(1));
  // Enter the else branch (growing memory by 2 pages).
  assertEquals(4, instance.exports.main(0));
})();

// This test verifies that the effects of growing memory in an if branch are
// retained when the branch is merged.
(function TestGrowMemoryAndStoreInIfBranchNoElse() {
  print("TestGrowMemoryAndStoreInIfBranchNoElse ...");
  let offset = 2*kPageSize - 4;
  let value = 42;
  let builder = generateBuilder();
  builder.addFunction("main", kSig_i_ii)
    .addBody([kExprGetLocal, 0,                // get condition parameter
              kExprI32Const, 1,
              kExprI32Eq,                      // compare it with 1
              kExprIf, kWasmStmt,              // if it's 1 then enter if
                kExprI32Const, 1,              // always grow by 1 page
                kExprGrowMemory, kMemoryZero,  // grow memory
                kExprDrop,                     // drop the result of grow
                kExprGetLocal, 1,              // get offset parameter
                kExprI32Const, value,          // put the value on stack
                kExprI32StoreMem, 0, 0,        // store
              kExprEnd,
              kExprGetLocal, 1,                // get offset parameter
              kExprI32LoadMem, 0, 0])          // load from grown memory
    .exportFunc();
  var instance = builder.instantiate();

  // Avoid the if branch (not growing memory).
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.main(0, offset));
  // Enter the if branch (growing memory).
  assertEquals(value, instance.exports.main(1, offset));
})();

// This test verifies that the effects of growing memory in an if branch are
// retained when the branch is merged even when  an else branch exists.
(function TestGrowMemoryAndStoreInIfBranchWithElse() {
  print("TestGrowMemoryAndStoreInIfBranchWithElse ...");
  let offset = 2*kPageSize - 4;
  let value = 42;
  let builder = generateBuilder();
  builder.addFunction("main", kSig_i_ii)
    .addBody([kExprGetLocal, 0,                // get condition parameter
              kExprI32Const, 1,
              kExprI32Eq,                      // compare it with 1
              kExprIf, kWasmStmt,              // if it's 1 then enter if
                kExprI32Const, 1,              // always grow by 1 page
                kExprGrowMemory, kMemoryZero,  // grow memory
                kExprDrop,                     // drop the result of grow
                kExprGetLocal, 1,              // get offset parameter
                kExprI32Const, value,          // put the value on stack
                kExprI32StoreMem, 0, 0,        // store
              kExprElse,
                kExprGetLocal, 1,              // get offset parameter
                kExprI32Const, value,          // put the value on stack
                kExprI32StoreMem, 0, 0,        // store
              kExprEnd,
              kExprGetLocal, 1,                // get offset parameter
              kExprI32LoadMem, 0, 0])          // load from grown memory
    .exportFunc();
  var instance = builder.instantiate();
  // Avoid the if branch (not growing memory).
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.main(0, offset));
  // Enter the if branch (growing memory).
  assertEquals(value, instance.exports.main(1, offset));
})();

// This test verifies that the effects of growing memory in an else branch are
// retained when the branch is merged.
(function TestGrowMemoryAndStoreInElseBranch() {
  print("TestGrowMemoryAndStoreInElseBranch ...");
  let offset = 2*kPageSize - 4;
  let value = 42;
  let builder = generateBuilder();
  builder.addFunction("main", kSig_i_ii)
    .addBody([kExprGetLocal, 0,                // get condition parameter
              kExprI32Const, 1,
              kExprI32Eq,                      // compare it with 1
              kExprIf, kWasmStmt,              // if it's 1 then enter if
                kExprGetLocal, 1,              // get offset parameter
                kExprI32Const, value,          // put the value on stack
                kExprI32StoreMem, 0, 0,        // store
              kExprElse,
                kExprI32Const, 1,              // always grow by 1 page
                kExprGrowMemory, kMemoryZero,  // grow memory
                kExprDrop,                     // drop the result of grow
                kExprGetLocal, 1,              // get offset parameter
                kExprI32Const, value,          // put the value on stack
                kExprI32StoreMem, 0, 0,        // store
              kExprEnd,
              kExprGetLocal, 1,                // get offset parameter
              kExprI32LoadMem, 0, 0])          // load from grown memory
    .exportFunc();
  var instance = builder.instantiate();
  // Avoid the else branch (not growing memory).
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.main(1, offset));
  // Enter the else branch (growing memory).
  assertEquals(value, instance.exports.main(0, offset));
})();

// This test verifies that the effects of growing memory in an if/else branch
// are retained when the branch is merged.
(function TestGrowMemoryAndStoreInBothIfAndElse() {
  print("TestGrowMemoryAndStoreInBothIfAndElse ...");
  let offset = 0;
  let if_value = 21;
  let else_value = 42;
  let builder = generateBuilder();
  builder.addFunction("main", kSig_i_ii)
    .addBody([kExprGetLocal, 0,                // get condition parameter
              kExprI32Const, 1,
              kExprI32Eq,                      // compare it with 1
              kExprIf, kWasmStmt,              // if it's 1 then enter if
                kExprI32Const, 1,              // always grow by 1 page
                kExprGrowMemory, kMemoryZero,  // grow memory
                kExprDrop,                     // drop the result of grow
                kExprGetLocal, 1,              // get offset parameter
                kExprI32Const, if_value,       // put the value on stack
                kExprI32StoreMem, 0, 0,        // store
              kExprElse,
                kExprI32Const, 2,              // always grow by 2 pages
                kExprGrowMemory, kMemoryZero,  // grow memory
                kExprDrop,                     // drop the result of grow
                kExprGetLocal, 1,              // get offset parameter
                kExprI32Const, else_value,     // put the value on stack
                kExprI32StoreMem, 0, 0,        // store
              kExprEnd,
              kExprGetLocal, 1,                // get offset parameter
              kExprI32LoadMem, 0, 0])          // load from grown memory
    .exportFunc();
  var instance = builder.instantiate();
  // Enter the if branch (growing memory by 1 page).
  assertEquals(if_value, instance.exports.main(1, offset));
  // Enter the else branch (growing memory by 2 pages).
  assertEquals(else_value, instance.exports.main(0, offset));
})();
