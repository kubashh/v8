// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler.h"
#include "src/codegen/tnode.h"
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
  TNode<HeapObject> incoming_value = CAST(Parameter(Descriptor::kValue));
  TNode<Object> incoming_handler = CAST(Parameter(Descriptor::kHandler));

  Label deoptimize(this), bailout(this), done(this);
  GotoIfNot(IsWeakFixedArrayMap(LoadMap(feedback)), &deoptimize);
  TNode<WeakFixedArray> polymorphic_array = CAST(feedback);

  // Iterate {feedback} array.
  const int kEntrySize = 2;

  // Load the {feedback} array length.
  TNode<IntPtrT> length = LoadAndUntagWeakFixedArrayLength(polymorphic_array);
  CSA_ASSERT(this, IntPtrLessThanOrEqual(IntPtrConstant(kEntrySize), length));

  TNode<Map> incoming_map = LoadMap(incoming_value);

  // TODO(gsathya): Unroll this loop to make it faster.
  //
  // This is a hand-crafted loop that iterates backwards and only compares
  // against zero at the end, since we already know that we will have at least a
  // single entry in the {feedback} array anyways.
  TVARIABLE(IntPtrT, var_index1, IntPtrSub(length, IntPtrConstant(kEntrySize)));
  Label loop1(this, &var_index1), loop_next1(this);
  Label try_migrate(this);

  TVARIABLE(IntPtrT, var_index2, IntPtrSub(length, IntPtrConstant(kEntrySize)));
  Label loop2(this, &var_index2), loop_next2(this);

  Goto(&loop1);
  BIND(&loop1);
  {
    TNode<MaybeObject> maybe_cached_map =
        LoadWeakFixedArrayElement(polymorphic_array, var_index1.value());
    CSA_ASSERT(this, IsWeakOrCleared(maybe_cached_map));
    GotoIfNot(IsWeakReferenceTo(maybe_cached_map, incoming_map), &loop_next1);

    // Found, now call handler.
    TNode<MaybeObject> handler = LoadWeakFixedArrayElement(
        polymorphic_array, var_index1.value(), kTaggedSize);
    GotoIf(TaggedEqual(handler, incoming_handler), &done);
    Return(IntPtrConstant(2));

    BIND(&loop_next1);
    var_index1 =
        Signed(IntPtrSub(var_index1.value(), IntPtrConstant(kEntrySize)));
    Branch(IntPtrGreaterThanOrEqual(var_index1.value(), IntPtrConstant(0)),
           &loop1, &try_migrate);
  }

  BIND(&try_migrate);
  {
    // TODO(gsathya): Bailout or deopt here?
    GotoIfNot(IsDeprecatedMap(incoming_map), &bailout);
    TNode<Object> result = CallRuntime(Runtime::kTryMigrateInstance,
                                       NoContextConstant(), incoming_value);
    GotoIf(TaggedIsSmi(result), &deoptimize);
    incoming_map = LoadMap(incoming_value);
    Goto(&loop2);
  }

  BIND(&loop2);
  {
    TNode<MaybeObject> maybe_cached_map =
        LoadWeakFixedArrayElement(polymorphic_array, var_index2.value());
    CSA_ASSERT(this, IsWeakOrCleared(maybe_cached_map));
    GotoIfNot(IsWeakReferenceTo(maybe_cached_map, incoming_map), &loop_next2);

    // Found, now call handler.
    TNode<MaybeObject> handler = LoadWeakFixedArrayElement(
        polymorphic_array, var_index2.value(), kTaggedSize);
    GotoIf(TaggedEqual(handler, incoming_handler), &done);
    Return(IntPtrConstant(2));

    BIND(&loop_next2);
    var_index2 =
        Signed(IntPtrSub(var_index2.value(), IntPtrConstant(kEntrySize)));
    Branch(IntPtrGreaterThanOrEqual(var_index2.value(), IntPtrConstant(0)),
           &loop2, &bailout);
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
