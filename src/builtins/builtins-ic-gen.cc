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
  TNode<HeapObject> feedback = CAST(Parameter(Descriptor::kFeedback));
  TNode<Map> incoming_map = CAST(Parameter(Descriptor::kMap));
  TNode<Object> incoming_handler = CAST(Parameter(Descriptor::kHandler));

  Label deoptimize(this), bailout(this), done(this);
  GotoIfNot(IsWeakFixedArrayMap(LoadMap(feedback)), &deoptimize);
  TNode<WeakFixedArray> polymorphic_array = CAST(feedback);

  // Iterate {feedback} array.
  const int kEntrySize = 2;

  // Load the {feedback} array length.
  TNode<IntPtrT> length = LoadAndUntagWeakFixedArrayLength(polymorphic_array);
  CSA_ASSERT(this, IntPtrLessThanOrEqual(IntPtrConstant(kEntrySize), length));

  // This is a hand-crafted loop that iterates backwards and only compares
  // against zero at the end, since we already know that we will have at least a
  // single entry in the {feedback} array anyways.
  TVARIABLE(IntPtrT, var_index, IntPtrSub(length, IntPtrConstant(kEntrySize)));
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
    GotoIfNot(TaggedEqual(handler, incoming_handler), &deoptimize);
    Goto(&done);

    BIND(&loop_next);
    var_index =
        Signed(IntPtrSub(var_index.value(), IntPtrConstant(kEntrySize)));
    Branch(IntPtrGreaterThanOrEqual(var_index.value(), IntPtrConstant(0)),
           &loop, &bailout);
  }
  /*
  TNode<IntPtrT> length = LoadAndUntagWeakFixedArrayLength(polymorphic_array);
  CSA_ASSERT(this, IntPtrLessThanOrEqual(
                       IntPtrConstant(FeedbackIterator::kEntrySize), length));

  GotoIf(IntPtrGreaterThanOrEqual(
             length, IntPtrConstant(4 * FeedbackIterator::kEntrySize)),
         &len4);
  GotoIf(IntPtrEqual(length, IntPtrConstant(3 * FeedbackIterator::kEntrySize)),
         &len3);
  GotoIf(IntPtrEqual(length, IntPtrConstant(2 * FeedbackIterator::kEntrySize)),
         &len2);
  GotoIf(IntPtrEqual(length, IntPtrConstant(1 * FeedbackIterator::kEntrySize)),
         &len1);

  Unreachable();

  BIND(&len4);
  {
    TNode<MaybeObject> maybe_cached_map =
        LoadWeakFixedArrayElement(polymorphic_array, IntPtrConstant(3));
    GotoIfNot(IsWeakReferenceTo(maybe_cached_map, incoming_map), &len3);
    TNode<MaybeObject> handler = LoadWeakFixedArrayElement(
        polymorphic_array, IntPtrConstant(3), kTaggedSize);
    Branch(TaggedEqual(incoming_handler, handler), &done, &deoptimize);
  }

  BIND(&len3);
  {
    TNode<MaybeObject> maybe_cached_map =
        LoadWeakFixedArrayElement(polymorphic_array, IntPtrConstant(2));
    GotoIfNot(IsWeakReferenceTo(maybe_cached_map, incoming_map), &len2);
    TNode<MaybeObject> handler = LoadWeakFixedArrayElement(
        polymorphic_array, IntPtrConstant(2), kTaggedSize);
    Branch(TaggedEqual(incoming_handler, handler), &done, &deoptimize);
  }

  BIND(&len2);
  {
    TNode<MaybeObject> maybe_cached_map =
        LoadWeakFixedArrayElement(polymorphic_array, IntPtrConstant(1));
    GotoIfNot(IsWeakReferenceTo(maybe_cached_map, incoming_map), &len1);
    TNode<MaybeObject> handler = LoadWeakFixedArrayElement(
        polymorphic_array, IntPtrConstant(1), kTaggedSize);
    Branch(TaggedEqual(incoming_handler, handler), &done, &deoptimize);
  }

  BIND(&len1);
  {
    TNode<MaybeObject> maybe_cached_map =
        LoadWeakFixedArrayElement(polymorphic_array, IntPtrConstant(0));
    GotoIfNot(IsWeakReferenceTo(maybe_cached_map, incoming_map), &bailout);
    TNode<MaybeObject> handler = LoadWeakFixedArrayElement(
        polymorphic_array, IntPtrConstant(0), kTaggedSize);
    Branch(TaggedEqual(incoming_handler, handler), &done, &deoptimize);
  }
  */

  BIND(&done);
  { Return(IntPtrConstant(0)); }

  BIND(&bailout);
  { Return(IntPtrConstant(1)); }

  BIND(&deoptimize);
  { Return(IntPtrConstant(2)); }
}

}  // namespace internal
}  // namespace v8
