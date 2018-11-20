// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

class BigIntBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit BigIntBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}
};

// https://tc39.github.io/proposal-bigint/#sec-to-big-int64
TF_BUILTIN(ToBigInt64, BigIntBuiltinsAssembler) {
  TVARIABLE(UintPtrT, var_low);
  TVARIABLE(UintPtrT, var_high);

  // input
  Node* value = Parameter(Descriptor::kArgument);
  Node* context = Parameter(Descriptor::kContext);

  TNode<BigInt> bigint = ToBigInt(CAST(context), CAST(value));

  // 2. Let int64bit be n modulo 2^64.
  // 3. If int64bit ≥ 2^63, return int64bit - 2^64;
  BigIntToRawBytes(bigint, &var_low, &var_high);

  TNode<IntPtrT> heapObjectTag = IntPtrConstant(kHeapObjectTag);
  TNode<IntPtrT> size = IntPtrConstant(kPointerSize);
  TNode<IntPtrT> two_times_size = IntPtrConstant(kPointerSize * 2);

  // otherwise return int64bit
  if (Is64()) {
    ReturnRaw(Signed(var_low.value()));
  } else {
    TNode<HeapObject> ptr =
        AllocateInNewSpace(IntPtrAdd(two_times_size, heapObjectTag));
    StoreMapNoWriteBarrier(ptr, RootIndex::kHeapNumberMap);

    // FIXME(sven): When GC or --verify-heap is enable, it will likely crash
    // here. From my last tests, the Object was marked as "strong" and became
    // part of the JS debugging routines which trapped.

    StoreNoWriteBarrier(MachineRepresentation::kWord32, ptr, var_high.value());
    StoreNoWriteBarrier(MachineRepresentation::kWord32, ptr,
                        IntPtrAdd(size, heapObjectTag), var_low.value());

    ReturnRaw(ptr);
  }
}

// https://tc39.github.io/proposal-bigint/#sec-bigint-constructor-number-value
TF_BUILTIN(NewBigInt, BigIntBuiltinsAssembler) {
  TVARIABLE(BigInt, value_bigint);

  Label is_int64(this);

  Goto(&is_int64);

  BIND(&is_int64);
  {
    if (Is64()) {
      TNode<IntPtrT> value =
          UncheckedCast<IntPtrT>(Parameter(Descriptor::kLow));

      value_bigint = BigIntFromInt64(value);
    } else {
      TNode<IntPtrT> low = UncheckedCast<IntPtrT>(Parameter(Descriptor::kLow));
      TNode<IntPtrT> high =
          UncheckedCast<IntPtrT>(Parameter(Descriptor::kHigh));

      /*
       * FIXME(sven): According to Wasm's calling conventions, in 32 bits mode;
       * %esi holds argument 1 aka High
       * %eax holds argument 2 aka Low
       *
       * That's confirmed by my observations, however `Descriptor::kHigh`
       * doesn't seem to map to the register
       * (which is probably because of the hacky way to call it).
       *
       * The compiler seems to have no knowledge about %esi, because it gets
       * overidden by some data at some point.
       */
      high = IntPtrConstant(0);

      value_bigint = BigIntFromInt32Pair(low, high);
    }
  }

  DCHECK(IsBigInt(value_bigint.value()));

  Return(value_bigint.value());
}

}  // namespace internal
}  // namespace v8
