// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-constructor-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-factory.h"
#include "src/code-stub-assembler.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

using compiler::Node;

class CollectionsBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit CollectionsBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  Node* AllocateJSMap(Node* js_map_function);
};

Node* CollectionsBuiltinsAssembler::AllocateJSMap(Node* js_map_function) {
  Node* const initial_map = LoadObjectField(
      js_map_function, JSFunction::kPrototypeOrInitialMapOffset);
  Node* const instance = AllocateJSObjectFromMap(initial_map);

  StoreObjectFieldRoot(instance, JSMap::kTableOffset,
                       Heap::kUndefinedValueRootIndex);

  return instance;
}

TF_BUILTIN(MapConstructor, CollectionsBuiltinsAssembler) {
  Node* const iterable = Parameter(Descriptor::kIterable);
  Node* const new_target = Parameter(Descriptor::kNewTarget);
  Node* const context = Parameter(Descriptor::kContext);

  Label if_targetisundefined(this, Label::kDeferred);
  GotoIf(IsUndefined(new_target), &if_targetisundefined);

  Node* const native_context = LoadNativeContext(context);
  Node* const js_map_fun =
      LoadContextElement(native_context, Context::JS_MAP_FUN_INDEX);

  VARIABLE(var_result, MachineRepresentation::kTagged, UndefinedConstant());

  Label init(this), exit(this), if_targetisnotmodified(this),
      if_targetismodified(this);
  Branch(WordEqual(js_map_fun, new_target), &if_targetisnotmodified,
         &if_targetismodified);

  BIND(&if_targetisnotmodified);
  {
    Node* const instance = AllocateJSMap(js_map_fun);
    var_result.Bind(instance);
    Goto(&init);
  }

  BIND(&if_targetismodified);
  {
    ConstructorBuiltinsAssembler constructor_assembler(this->state());
    Node* const instance = constructor_assembler.EmitFastNewObject(
        context, js_map_fun, new_target);
    var_result.Bind(instance);
    Goto(&init);
  }

  BIND(&init);
  // TODO(gsathya): Remove runtime call once OrderedHashTable is ported.
  CallRuntime(Runtime::kMapInitialize, NoContextConstant(), var_result.value());

  GotoIf(WordEqual(iterable, UndefinedConstant()), &exit);
  GotoIf(WordEqual(iterable, NullConstant()), &exit);

  Label if_notcallable(this);
  // TODO(gsathya): Add fast path for unmodified maps.
  Node* const adder = GetProperty(context, var_result.value(),
                                  isolate()->factory()->set_string());
  GotoIf(TaggedIsSmi(adder), &if_notcallable);
  Node* const adder_map = LoadMap(adder);
  GotoIfNot(IsCallableMap(adder_map), &if_notcallable);

  Node* const iterator = GetIterator(context, iterable);
  GotoIf(WordEqual(iterator, UndefinedConstant()), &exit);

  Node* const fast_iterator_result_map =
      LoadContextElement(native_context, Context::ITERATOR_RESULT_MAP_INDEX);

  VARIABLE(var_exception, MachineRepresentation::kTagged, UndefinedConstant());

  Label loop(this), if_notobject(this), if_exception(this);
  Goto(&loop);

  BIND(&loop);
  {
    Node* const next =
        IteratorStep(context, iterator, &exit, fast_iterator_result_map);

    Node* const next_value =
        IteratorValue(context, next, fast_iterator_result_map);

    GotoIf(TaggedIsSmi(next_value), &if_notobject);
    GotoIfNot(IsJSReceiver(next_value), &if_notobject);

    Node* const k =
        GetProperty(context, next_value, isolate()->factory()->zero_string());
    GotoIfException(k, &if_exception, &var_exception);

    Node* const v =
        GetProperty(context, next_value, isolate()->factory()->one_string());
    GotoIfException(v, &if_exception, &var_exception);

    Node* add_call = CallJS(CodeFactory::Call(isolate()), context, adder,
                            var_result.value(), k, v);
    GotoIfException(add_call, &if_exception, &var_exception);
    Goto(&loop);

    BIND(&if_notobject);
    {
      Node* const exception = MakeTypeError(
          MessageTemplate::kIteratorResultNotAnObject, next_value);
      var_exception.Bind(exception);
      Goto(&if_exception);
    }
  }

  BIND(&if_exception);
  {
    IteratorClose(context, iterator, var_exception.value());
    CallRuntime(Runtime::kReThrow, context, var_exception.value());
    Unreachable();
  }

  BIND(&if_notcallable);
  {
    Node* const message_id = SmiConstant(MessageTemplate::kPropertyNotFunction);
    CallRuntime(Runtime::kThrowTypeError, context, message_id, new_target);
    Unreachable();
  }

  BIND(&if_targetisundefined);
  {
    Node* const message_id =
        SmiConstant(MessageTemplate::kConstructorNotFunction);
    CallRuntime(Runtime::kThrowTypeError, context, message_id, new_target);
    Unreachable();
  }

  BIND(&exit);
  Return(var_result.value());
}

}  // namespace internal
}  // namespace v8
