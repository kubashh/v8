// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/codegen/compiler.h"
#include "src/logging/counters.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-async-context-inl.h"
#include "src/objects/js-async-context.h"
#include "src/objects/ordered-hash-table.h"

namespace v8 {
namespace internal {

BUILTIN(AsyncContextVariablePrototypeRun) {
  HandleScope scope(isolate);

  Handle<Object> receiver = args.receiver();
  Handle<Object> value = args.atOrUndefined(isolate, 1);
  Handle<Object> target = args.atOrUndefined(isolate, 2);

  // 1. Let asyncVariable be the this value.
  // 2. Perform ? RequireInternalSlot(asyncVariable, [[AsyncVariableName]]).
  if (!IsJSAsyncContextVariable(*receiver)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIncompatibleMethodReceiver));
  }
  Handle<JSAsyncContextVariable> async_variable =
      Handle<JSAsyncContextVariable>::cast(receiver);

  if (!IsCallable(*target)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, target));
  }

  // 3. Let previousContextMapping be AsyncContextSnapshot().
  Handle<HeapObject> snapshot =
      Handle<HeapObject>(isolate->heap()->async_context_store(), isolate);
  // 4. Let asyncContextMapping be a new empty List.
  Handle<OrderedHashMap> async_context_store;

  // 5. For each Async Context Mapping Record p of previousContextMapping, do
  //   a. If SameValueZero(p.[[AsyncContextKey]], asyncVariable) is false, then
  //     i. Let q be the Async Context Mapping Record { [[AsyncContextKey]]:
  //     p.[[AsyncContextKey]], [[AsyncContextValue]]: p.[[AsyncContextValue]]
  //     }. ii. Append q to asyncContextMapping.
  // 6. Assert: asyncContextMapping does not contain an Async Context Mapping
  // Record whose [[AsyncContextKey]] is asyncVariable.
  // 7. Let p be the Async Context Mapping Record { [[AsyncContextKey]]:
  // asyncVariable, [[AsyncContextValue]]: value }.
  // 8. Append p to asyncContextMapping.
  InternalIndex entry_index = InternalIndex::NotFound();
  if (!IsUndefined(*snapshot)) {
    Handle<OrderedHashMap> snapshot_map =
        Handle<OrderedHashMap>::cast(snapshot);
    int capacity = snapshot_map->NumberOfElements();
    if (!OrderedHashMap::HasKey(isolate, *snapshot_map, *async_variable)) {
      capacity += 1;
    }

    async_context_store =
        OrderedHashMap::Allocate(isolate, capacity).ToHandleChecked();
    async_context_store->CopyElements(isolate, 0, *snapshot_map, 0,
                                      snapshot_map->length(),
                                      WriteBarrierMode::SKIP_WRITE_BARRIER);
    entry_index = async_context_store->FindEntry(isolate, *async_variable);
  } else {
    async_context_store =
        OrderedHashMap::Allocate(isolate, 1).ToHandleChecked();
  }

  if (entry_index.is_found()) {
    async_context_store->SetEntry(entry_index, *async_variable, *value);
  } else {
    async_context_store =
        OrderedHashMap::Add(isolate, async_context_store, async_variable, value)
            .ToHandleChecked();
  }

  // 9. AsyncContextSwap(asyncContextMapping).
  isolate->roots_table()
      .slot(i::RootIndex::kAsyncContextStore)
      .store(*async_context_store);

  base::ScopedVector<Handle<Object>> argv(std::max(0, args.length() - 3));
  for (int i = 3; i < args.length(); ++i) {
    argv[i - 3] = args.at(i);
  }

  Handle<Object> result;
  // 10. Let result be Completion(Call(func, undefined, args)).
  if (!Execution::Call(isolate, target, isolate->factory()->undefined_value(),
                       argv.length(), argv.data())
           .ToHandle(&result)) {
    DCHECK((isolate)->has_pending_exception());
    // 11. AsyncContextSwap(previousContextMapping).
    isolate->roots_table()
        .slot(i::RootIndex::kAsyncContextStore)
        .store(*snapshot);
    // 12. Return result.
    return ReadOnlyRoots(isolate).exception();
  }

  // 11. AsyncContextSwap(previousContextMapping).
  isolate->roots_table()
      .slot(i::RootIndex::kAsyncContextStore)
      .store(*snapshot);
  // 12. Return result.
  return *result;
}

BUILTIN(AsyncContextVariablePrototypeGet) {
  HandleScope scope(isolate);
  Handle<Object> receiver = args.receiver();

  // 1. Let asyncVariable be the this value.
  // 2. Perform ? RequireInternalSlot(asyncVariable,
  // [[AsyncVariableDefaultValue]]).
  if (!IsJSAsyncContextVariable(*receiver)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIncompatibleMethodReceiver));
  }
  Handle<JSAsyncContextVariable> async_variable =
      Handle<JSAsyncContextVariable>::cast(receiver);

  // 3. Let agentRecord be the surrounding agent's Agent Record.
  // 4. Let asyncContextMapping be agentRecord.[[AsyncContextMapping]].
  Handle<Object> snapshot =
      Handle<Object>(isolate->heap()->async_context_store(), isolate);

  // 5. For each Async Context Mapping Record p of asyncContextMapping, do
  //   a. If SameValueZero(p.[[AsyncContextKey]], asyncVariable) is true, return
  //   p.[[AsyncContextValue]].
  // 6. Return asyncVariable.[[AsyncVariableDefaultValue]].

  if (IsUndefined(*snapshot)) {
    return async_variable->defaultValue();
  }

  Handle<OrderedHashMap> async_context_store =
      Handle<OrderedHashMap>::cast(snapshot);
  InternalIndex found =
      async_context_store->FindEntry(isolate, *async_variable);
  if (found.is_not_found()) {
    return async_variable->defaultValue();
  }

  return async_context_store->ValueAt(found);
}

BUILTIN(AsyncContextSnapshotConstructor) {
  HandleScope scope(isolate);

  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (IsUndefined(*args.new_target())) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kConstructorNotFunction,
                     isolate->factory()->AsyncContextSnapshot_string()));
  }

  // 2. Let snapshotMapping be AsyncContextSnapshot();
  Handle<HeapObject> snapshot_mapping =
      Handle<HeapObject>(isolate->heap()->async_context_store(), isolate);

  // 3. Let asyncSnapshot be ? OrdinaryCreateFromConstructor(NewTarget,
  // "%AsyncContext.Snapshot.prototype%", « [[AsyncSnapshotMapping]] »).
  Handle<JSFunction> target = args.target();
  Handle<JSReceiver> new_target = Handle<JSReceiver>::cast(args.new_target());
  Handle<JSObject> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      JSObject::New(target, new_target, Handle<AllocationSite>::null()));
  Handle<JSAsyncContextSnapshot> async_snapshot =
      Handle<JSAsyncContextSnapshot>::cast(result);

  // 4. Set asyncSnapshot.[[AsyncSnapshotMapping]] to snapshotMapping.
  async_snapshot->set_snapshot(*snapshot_mapping);

  // 5. Return asyncSnapshot.
  return *async_snapshot;
}

BUILTIN(AsyncContextSnapshotPrototypeRun) {
  HandleScope scope(isolate);

  Handle<Object> receiver = args.receiver();
  Handle<Object> func = args.atOrUndefined(isolate, 1);

  // 1. Let asyncSnapshot be the this value.
  // 2. Perform ? RequireInternalSlot(asyncSnapshot, [[AsyncSnapshotMapping]]).
  if (!IsJSAsyncContextSnapshot(*receiver)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIncompatibleMethodReceiver));
  }

  if (!IsCallable(*func)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, func));
  }

  // 3. Let previousContextMapping be
  // AsyncContextSwap(asyncSnapshot.[[AsyncSnapshotMapping]]).
  Tagged<HeapObject> snapshot_mapping =
      Handle<JSAsyncContextSnapshot>::cast(receiver)->snapshot();
  Handle<HeapObject> previous_context_mapping =
      Handle<HeapObject>(isolate->heap()->async_context_store(), isolate);
  isolate->roots_table()
      .slot(i::RootIndex::kAsyncContextStore)
      .store(snapshot_mapping);

  base::ScopedVector<Handle<Object>> argv(std::max(0, args.length() - 2));
  for (int i = 2; i < args.length(); ++i) {
    argv[i - 2] = args.at(i);
  }

  Handle<Object> result;
  // 4. Let result be Completion(Call(func, undefined, args)).
  if (!Execution::Call(isolate, func, isolate->factory()->undefined_value(),
                       argv.length(), argv.data())
           .ToHandle(&result)) {
    DCHECK((isolate)->has_pending_exception());
    // 5. AsyncContextSwap(previousContextMapping).
    isolate->roots_table()
        .slot(i::RootIndex::kAsyncContextStore)
        .store(*previous_context_mapping);
    // 6. Return result.
    return ReadOnlyRoots(isolate).exception();
  }

  // 5. AsyncContextSwap(previousContextMapping).
  isolate->roots_table()
      .slot(i::RootIndex::kAsyncContextStore)
      .store(*previous_context_mapping);
  // 6. Return result.
  return *result;
}

}  // namespace internal
}  // namespace v8
