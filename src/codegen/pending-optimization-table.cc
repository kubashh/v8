// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/pending-optimization-table.h"

#include "src/execution/isolate-inl.h"
#include "src/heap/heap-inl.h"
#include "src/objects/hash-table.h"
#include "src/objects/js-objects.h"

namespace v8 {
namespace internal {

void PendingOptimizationTable::PreparedForOptimization(
    Isolate* isolate, Handle<JSFunction> function) {
  if (!FLAG_testing_d8_test_runner) return;

  Handle<ObjectHashTable> table =
      isolate->heap()->pending_optimize_for_test_bytecode().IsUndefined()
          ? ObjectHashTable::New(isolate, 1)
          : handle(ObjectHashTable::cast(
                       isolate->heap()->pending_optimize_for_test_bytecode()),
                   isolate);
  Handle<Tuple2> tuple = isolate->factory()->NewTuple2(
      handle(function->shared().GetBytecodeArray(), isolate),
      handle(Smi::FromInt(FunctionStatus::kPrepareForOptimize), isolate),
      AllocationType::kYoung);
  table = ObjectHashTable::Put(table, function, tuple);
  isolate->heap()->SetPendingOptimizeForTestBytecode(*table);
}

void PendingOptimizationTable::MarkedForOptimization(
    Isolate* isolate, Handle<JSFunction> function) {
  if (!FLAG_testing_d8_test_runner) return;

  if (isolate->heap()->pending_optimize_for_test_bytecode().IsUndefined() ||
      ObjectHashTable::cast(
          isolate->heap()->pending_optimize_for_test_bytecode())
          .Lookup(function)
          .IsTheHole()) {
    PrintF("Error: Function ");
    function->ShortPrint();
    PrintF(
        " should be prepared for optimization with "
        "%%PrepareFunctionForOptimize before  "
        "%%OptimizeFunctionOnNextCall / %%OptimizeOSR ");
    UNREACHABLE();
    return;
  }

  Handle<ObjectHashTable> table(
      ObjectHashTable::cast(
          isolate->heap()->pending_optimize_for_test_bytecode()),
      isolate);
  Handle<Tuple2> tuple = isolate->factory()->NewTuple2(
      handle(function->shared().GetBytecodeArray(), isolate),
      handle(Smi::FromInt(FunctionStatus::kMarkForOptimize), isolate),
      AllocationType::kYoung);
  table = ObjectHashTable::Put(table, function, tuple);
  isolate->heap()->SetPendingOptimizeForTestBytecode(*table);
}

void PendingOptimizationTable::FunctionWasOptimized(
    Isolate* isolate, Handle<JSFunction> function) {
  if (!FLAG_testing_d8_test_runner ||
      isolate->heap()->pending_optimize_for_test_bytecode().IsUndefined()) {
    return;
  }

  Handle<ObjectHashTable> table =
      handle(ObjectHashTable::cast(
                 isolate->heap()->pending_optimize_for_test_bytecode()),
             isolate);
  Handle<Object> value(table->Lookup(function), isolate);
  // Remove only if we have already seen %OptimizeFunctionOnNextCall. If it is
  // optimized for other reasons, still keep holding the bytecode since we may
  // optimize it later.
  if (!value->IsTheHole() &&
      Smi::cast(Handle<Tuple2>::cast(value)->value2()).value() ==
          FunctionStatus::kMarkForOptimize) {
    bool was_present;
    table = table->Remove(isolate, table, function, &was_present);
    isolate->heap()->SetPendingOptimizeForTestBytecode(*table);
  }
}

}  // namespace internal
}  // namespace v8
