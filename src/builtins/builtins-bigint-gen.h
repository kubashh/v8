// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_BIGINT_GEN_H_
#define V8_BUILTINS_BUILTINS_BIGINT_GEN_H_

#include "src/codegen/code-stub-assembler.h"
#include "src/objects/bigint.h"

namespace v8 {
namespace internal {

class BigIntBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit BigIntBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  // CSA has:
  // StoreBigIntBitfield
  // StoreBigIntDigit
  // LoadBigIntBitfield
  // LoadBigIntDigit

  // TODO(nicohartmann): unify with src/builtins/builtins-data-view-gen.h
  // implementation
  TNode<Uint32T> EncodeBigIntBits(bool sign, intptr_t length) {
    return Unsigned(
        Int32Constant(BigInt::SignBits::encode(sign) |
                      BigInt::LengthBits::encode(static_cast<int>(length))));
  }

  TNode<Uint32T> EncodeBigIntBitfield(TNode<BoolT> sign,
                                      TNode<IntPtrT> length) {
    return Unsigned(
        Word32Or(Word32Shl(TruncateIntPtrToInt32(length),
                           Int32Constant(BigIntBase::LengthBits::kShift)),
                 Word32And(sign, Int32Constant(BigIntBase::SignBits::kMask))));
  }

  TNode<IntPtrT> DecodeBigIntLength(TNode<BigInt> value) {
    TNode<Word32T> bitfield = LoadBigIntBitfield(value);
    return ChangeInt32ToIntPtr(
        Signed(DecodeWord32<BigIntBase::LengthBits>(bitfield)));
  }

  TNode<Uint32T> DecodeBigIntSign(TNode<BigInt> value) {
    TNode<Word32T> bitfield = LoadBigIntBitfield(value);
    return DecodeWord32<BigIntBase::SignBits>(bitfield);
  }

  void TrimMutableBigInt(TNode<BigInt> result, TNode<IntPtrT> new_length,
                         TNode<IntPtrT> to_trim) {
    TNode<ExternalReference> mutable_big_int_make_immutable_helper =
        ExternalConstant(ExternalReference::
                             mutable_big_int_make_immutable_helper_function());
    CallCFunction(mutable_big_int_make_immutable_helper,
                  MachineType::AnyTagged(),
                  std::make_pair(MachineType::AnyTagged(), result),
                  std::make_pair(MachineType::IntPtr(), new_length),
                  std::make_pair(MachineType::IntPtr(), to_trim));
  }

  void CppAbsoluteAdd(TNode<BigInt> result, TNode<BigInt> x, TNode<BigInt> y) {
    TNode<ExternalReference> mutable_big_int_absolute_add_helper =
        ExternalConstant(
            ExternalReference::mutable_big_int_absolute_add_helper_function());
    CallCFunction(mutable_big_int_absolute_add_helper, MachineType::AnyTagged(),
                  std::make_pair(MachineType::AnyTagged(), result),
                  std::make_pair(MachineType::AnyTagged(), x),
                  std::make_pair(MachineType::AnyTagged(), y));
  }
};

}  // namespace internal
}  // namespace v8
#endif  // V8_BUILTINS_BUILTINS_BIGINT_GEN_H_
