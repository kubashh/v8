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

  // otherwise return int64bit
  if (Is64()) {
    ReturnRaw(Signed(var_low.value()));
  } else {
    /* TNode<IntPtrT> heapObjectTag = IntPtrConstant(kHeapObjectTag); */
    TNode<IntPtrT> heapObjectTag = IntPtrConstant(0);
    TNode<IntPtrT> size = IntPtrConstant(sizeof(int32_t));
    TNode<IntPtrT> two_times_size = IntPtrConstant(sizeof(int32_t) * 2);

    /**
     * The memory layout is:
     * | 1 byte | 4 bytes | 4 bytes |
     * | GC map |   low   |  high   |
     *
     * FIXME(ssauleau): verifying the heap will crash because of this malformed
     * pointer.
     */
    TNode<HeapObject> ptr =
        AllocateInNewSpace(IntPtrAdd(heapObjectTag, two_times_size));
    /* StoreMapNoWriteBarrier(ptr, RootIndex::kHeapNumberMap); */

    StoreNoWriteBarrier(MachineRepresentation::kWord32,  // rep
                        ptr,                             // base
                        heapObjectTag,                   // offset
                        var_low.value());                // value
    StoreNoWriteBarrier(MachineRepresentation::kWord32,  // rep
                        ptr,                             // base
                        IntPtrAdd(heapObjectTag, size),  // offset
                        var_high.value());               // value

    ReturnRaw(ptr);
  }
}

// https://tc39.github.io/proposal-bigint/#sec-bigint-constructor-number-value
TF_BUILTIN(NewBigInt, BigIntBuiltinsAssembler) {
  if (Is64()) {
    TVARIABLE(BigInt, value_bigint);

    TNode<IntPtrT> argument =
        UncheckedCast<IntPtrT>(Parameter(Descriptor::kArgument));

    value_bigint = BigIntFromInt64(argument);
    DCHECK(IsBigInt(value_bigint.value()));

    Return(value_bigint.value());
  } else {
    Unreachable();
  }
}

// https://tc39.github.io/proposal-bigint/#sec-bigint-constructor-number-value
TF_BUILTIN(NewBigInt32, BigIntBuiltinsAssembler) {
  if (Is32()) {
    TVARIABLE(BigInt, value_bigint);

    TNode<IntPtrT> low = UncheckedCast<IntPtrT>(Parameter(Descriptor::kLow));
    TNode<IntPtrT> high = UncheckedCast<IntPtrT>(Parameter(Descriptor::kHigh));

    value_bigint = BigIntFromInt32Pair(low, high);
    DCHECK(IsBigInt(value_bigint.value()));

    Return(value_bigint.value());
  } else {
    Unreachable();
  }
}

}  // namespace internal
}  // namespace v8
