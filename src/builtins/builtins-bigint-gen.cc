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

  TNode<Object> value = CAST(Parameter(Descriptor::kArgument));
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));

  TNode<BigInt> bigint = ToBigInt(context, value);

  // 2. Let int64bit be n modulo 2^64.
  // 3. If int64bit ≥ 2^63, return int64bit - 2^64;
  BigIntToRawBytes(bigint, &var_low, &var_high);

  if (Is64()) {
    ReturnRaw(var_low.value());
  } else {
    ReturnRaw(var_high.value(), var_low.value());
  }
}

// https://tc39.github.io/proposal-bigint/#sec-bigint-constructor-number-value
TF_BUILTIN(NewBigInt, BigIntBuiltinsAssembler) {
  if (Is64()) {
    TNode<IntPtrT> argument =
        UncheckedCast<IntPtrT>(Parameter(Descriptor::kArgument));

    Return(BigIntFromInt64(argument));
  } else {
    Unreachable();
  }
}

// https://tc39.github.io/proposal-bigint/#sec-bigint-constructor-number-value
TF_BUILTIN(NewBigInt32, BigIntBuiltinsAssembler) {
  if (Is32()) {
    TNode<IntPtrT> low = UncheckedCast<IntPtrT>(Parameter(Descriptor::kLow));
    TNode<IntPtrT> high = UncheckedCast<IntPtrT>(Parameter(Descriptor::kHigh));

    Return(BigIntFromInt32Pair(low, high));
  } else {
    Unreachable();
  }
}

}  // namespace internal
}  // namespace v8
