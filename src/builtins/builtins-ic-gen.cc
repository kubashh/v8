// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler.h"
#include "src/common/globals.h"
#include "src/ic/accessor-assembler.h"
#include "src/objects/feedback-vector.h"

namespace v8 {
namespace internal {

#define IC_BUILTIN(Name)                                                \
  void Builtins::Generate_##Name(compiler::CodeAssemblerState* state) { \
    AccessorAssembler assembler(state);                                 \
    assembler.Generate##Name();                                         \
  }

#define IC_BUILTIN_PARAM(BuiltinName, GeneratorName, parameter)                \
  void Builtins::Generate_##BuiltinName(compiler::CodeAssemblerState* state) { \
    AccessorAssembler assembler(state);                                        \
    assembler.Generate##GeneratorName(parameter);                              \
  }

IC_BUILTIN(LoadIC)
IC_BUILTIN(LoadIC_Megamorphic)
IC_BUILTIN(LoadIC_Noninlined)
IC_BUILTIN(LoadIC_NoFeedback)
IC_BUILTIN(LoadICTrampoline)
IC_BUILTIN(LoadICTrampoline_Megamorphic)
IC_BUILTIN(KeyedLoadIC)
IC_BUILTIN(KeyedLoadIC_Megamorphic)
IC_BUILTIN(KeyedLoadIC_PolymorphicName)
IC_BUILTIN(KeyedLoadICTrampoline)
IC_BUILTIN(KeyedLoadICTrampoline_Megamorphic)
IC_BUILTIN(LoadGlobalIC_NoFeedback)
IC_BUILTIN(StoreGlobalIC)
IC_BUILTIN(StoreGlobalICTrampoline)
IC_BUILTIN(StoreIC)
IC_BUILTIN(StoreICTrampoline)
IC_BUILTIN(KeyedStoreIC)
IC_BUILTIN(KeyedStoreICTrampoline)
IC_BUILTIN(StoreInArrayLiteralIC)
IC_BUILTIN(CloneObjectIC)
IC_BUILTIN(CloneObjectIC_Slow)
IC_BUILTIN(KeyedHasIC)
IC_BUILTIN(KeyedHasIC_Megamorphic)
IC_BUILTIN(KeyedHasIC_PolymorphicName)

IC_BUILTIN_PARAM(LoadGlobalIC, LoadGlobalIC, NOT_INSIDE_TYPEOF)
IC_BUILTIN_PARAM(LoadGlobalICInsideTypeof, LoadGlobalIC, INSIDE_TYPEOF)
IC_BUILTIN_PARAM(LoadGlobalICTrampoline, LoadGlobalICTrampoline,
                 NOT_INSIDE_TYPEOF)
IC_BUILTIN_PARAM(LoadGlobalICInsideTypeofTrampoline, LoadGlobalICTrampoline,
                 INSIDE_TYPEOF)

TF_BUILTIN(DynamicMapChecks, CodeStubAssembler) {
  TNode<Object> feedback_vector = CAST(Parameter(Descriptor::kFeedbackVector));
  TNode<IntPtrT> slot_index =
      Signed(BitcastTaggedToWord(Parameter(Descriptor::kSlotIndex)));
  TNode<HeapObject> incoming_value = CAST(Parameter(Descriptor::kValue));
  TNode<Map> incoming_map = CAST(Parameter(Descriptor::kMap));
  TNode<Object> handler_to_check = CAST(Parameter(Descriptor::kHandler));

  Label deoptimize(this), bailout(this), done(this), poly_check(this),
      handler_check(this);

  int32_t header_size = FeedbackVector::kFeedbackSlotsOffset - kHeapObjectTag;
  TNode<IntPtrT> offset = ElementOffsetFromIndex(slot_index, HOLEY_ELEMENTS);
  TNode<MaybeObject> feedback = ReinterpretCast<MaybeObject>(
      Load(MachineType::AnyTagged(), feedback_vector,
           IntPtrAdd(offset, IntPtrConstant(header_size))));

  GotoIf(IsWeakReferenceTo(feedback, incoming_map), &handler_check);

  // Feedback is poly or mega
  GotoIf(IsStrong(feedback), &poly_check);

  // We are mono but the incoming map is not deprecated.. we will go
  // poly next or the mono map in feedback will be updated to the
  // stable map, so let's bailout.
  GotoIfNot(IsDeprecatedMap(incoming_map), &bailout);

  // Try to update our deprecated map and check again
  TNode<Object> result = CallRuntime(Runtime::kTryMigrateInstance,
                                     NoContextConstant(), incoming_value);
  GotoIf(TaggedIsSmi(result), &deoptimize);
  incoming_map = LoadMap(incoming_value);

  GotoIfNot(IsWeakReferenceTo(feedback, incoming_map), &bailout);
  Goto(&handler_check);

  BIND(&handler_check);
  {
    TNode<Object> mono_handler = ReinterpretCast<Object>(
        Load(MachineType::AnyTagged(), feedback_vector,
             IntPtrAdd(offset, IntPtrConstant(header_size + kTaggedSize))));
    GotoIf(TaggedEqual(mono_handler, handler_to_check), &done);
    Goto(&deoptimize);
  }

  BIND(&poly_check);
  {
    TNode<HeapObject> strong_feedback =
        GetHeapObjectIfStrong(feedback, &deoptimize);
    GotoIfNot(IsWeakFixedArrayMap(LoadMap(strong_feedback)), &deoptimize);
    TNode<WeakFixedArray> polymorphic_array = CAST(strong_feedback);
    incoming_map = LoadMap(incoming_value);

    // Iterate {feedback} array.
    const int kEntrySize = 2;

    // Load the {feedback} array length.
    TNode<IntPtrT> length = LoadAndUntagWeakFixedArrayLength(polymorphic_array);
    CSA_ASSERT(this, IntPtrLessThanOrEqual(IntPtrConstant(kEntrySize), length));

    // TODO(gsathya): Unroll this loop to make it faster.
    //
    // This is a hand-crafted loop that iterates backwards and only compares
    // against zero at the end, since we already know that we will have at least
    // a single entry in the {feedback} array anyways.
    TVARIABLE(IntPtrT, var_index,
              IntPtrSub(length, IntPtrConstant(kEntrySize)));
    Label loop(this, &var_index), loop_next(this);
    Goto(&loop);

    BIND(&loop);
    {
      TNode<MaybeObject> maybe_cached_map =
          LoadWeakFixedArrayElement(polymorphic_array, var_index.value());
      CSA_ASSERT(this, IsWeakOrCleared(maybe_cached_map));
      GotoIfNot(IsWeakReferenceTo(maybe_cached_map, incoming_map), &loop_next);

      // Found, now call handler.
      TNode<MaybeObject> handler = LoadWeakFixedArrayElement(
          polymorphic_array, var_index.value(), kTaggedSize);
      GotoIfNot(TaggedEqual(handler, handler_to_check), &deoptimize);
      Goto(&done);

      BIND(&loop_next);
      var_index =
          Signed(IntPtrSub(var_index.value(), IntPtrConstant(kEntrySize)));
      Branch(IntPtrGreaterThanOrEqual(var_index.value(), IntPtrConstant(0)),
             &loop, &bailout);
    }
  }

  BIND(&done);
  { Return(IntPtrConstant(0)); }

  BIND(&bailout);
  { Return(IntPtrConstant(1)); }

  BIND(&deoptimize);
  { Return(IntPtrConstant(2)); }
}

}  // namespace internal
}  // namespace v8
