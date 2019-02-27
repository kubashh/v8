// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

// https://tc39.github.io/proposal-bigint/#sec-to-big-int64
TF_BUILTIN(BigIntToI64, CodeStubAssembler) {
  TNode<Object> value = CAST(Parameter(Descriptor::kArgument));
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TNode<BigInt> bigint = ToBigInt(context, value);

  TVARIABLE(UintPtrT, var_low);
  TVARIABLE(UintPtrT, var_high);

#if V8_TARGET_ARCH_64_BIT
  // 2. Let int64bit be n modulo 2^64.
  // 3. If int64bit ≥ 2^63, return int64bit - 2^64;
  BigIntToRawBytes(bigint, &var_low, &var_high);
  ReturnRaw(var_low.value());
#else
  // 2. Let int64bit be n modulo 2^64.
  // 3. If int64bit ≥ 2^63, return int64bit - 2^64;
  BigIntToRawBytes(bigint, &var_low, &var_high);
  ReturnPair(var_low.value(), var_high.value());
#endif  // V8_TARGET_ARCH_64_BIT
}

// https://tc39.github.io/proposal-bigint/#sec-bigint-constructor-number-value
TF_BUILTIN(I64ToBigInt, CodeStubAssembler) {
#if V8_TARGET_ARCH_64_BIT
  TNode<IntPtrT> argument =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kArgument));

  Return(BigIntFromInt64(argument));
#else
  TNode<IntPtrT> low = UncheckedCast<IntPtrT>(Parameter(Descriptor::kLow));
  TNode<IntPtrT> high = UncheckedCast<IntPtrT>(Parameter(Descriptor::kHigh));

  Return(BigIntFromInt32Pair(low, high));
#endif  // V8_TARGET_ARCH_64_BIT
}

}  // namespace internal
}  // namespace v8
