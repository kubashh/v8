// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-bigint-gen.h"

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/handles/handles-inl.h"
#include "src/objects/dictionary.h"
#include "src/objects/slots-inl.h"

namespace v8 {
namespace internal {

// https://tc39.github.io/proposal-bigint/#sec-to-big-int64
TF_BUILTIN(BigIntToI64, CodeStubAssembler) {
  if (!Is64()) {
    Unreachable();
    return;
  }

  auto value = Parameter<Object>(Descriptor::kArgument);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<BigInt> n = ToBigInt(context, value);

  TVARIABLE(UintPtrT, var_low);
  TVARIABLE(UintPtrT, var_high);

  BigIntToRawBytes(n, &var_low, &var_high);
  Return(var_low.value());
}

// https://tc39.github.io/proposal-bigint/#sec-to-big-int64
TF_BUILTIN(BigIntToI32Pair, CodeStubAssembler) {
  if (!Is32()) {
    Unreachable();
    return;
  }

  auto value = Parameter<Object>(Descriptor::kArgument);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<BigInt> bigint = ToBigInt(context, value);

  TVARIABLE(UintPtrT, var_low);
  TVARIABLE(UintPtrT, var_high);

  BigIntToRawBytes(bigint, &var_low, &var_high);
  Return(var_low.value(), var_high.value());
}

// https://tc39.github.io/proposal-bigint/#sec-bigint-constructor-number-value
TF_BUILTIN(I64ToBigInt, CodeStubAssembler) {
  if (!Is64()) {
    Unreachable();
    return;
  }

  auto argument = UncheckedParameter<IntPtrT>(Descriptor::kArgument);

  Return(BigIntFromInt64(argument));
}

// https://tc39.github.io/proposal-bigint/#sec-bigint-constructor-number-value
TF_BUILTIN(I32PairToBigInt, CodeStubAssembler) {
  if (!Is32()) {
    Unreachable();
    return;
  }

  auto low = UncheckedParameter<IntPtrT>(Descriptor::kLow);
  auto high = UncheckedParameter<IntPtrT>(Descriptor::kHigh);

  Return(BigIntFromInt32Pair(low, high));
}

}  // namespace internal
}  // namespace v8

// TODO(nicohartmann@): Put the following stuff into its own file.
#undef BIND

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/define-assembler-macros.inc"

namespace v8::internal {

using namespace compiler::turboshaft;

class TurboshaftBuiltinAssembler : public compiler::turboshaft::TSAssembler<> {
 public:
  TurboshaftBuiltinAssembler(Isolate* isolate,
                             compiler::turboshaft::Graph& graph, Zone* zone)
      : TSAssembler(graph, graph, zone), isolate_(isolate) {}

  V<WordPtr> ReadBigIntLength(V<BigInt> value) {
    V<Word32> bitfield = LoadBigIntBitfield(value);
    return __ ChangeInt32ToIntPtr(
        DecodeWord32<BigIntBase::LengthBits>(bitfield));
  }

  V<Word32> ReadBigIntSign(V<BigInt> value) {
    V<Word32> bitfield = LoadBigIntBitfield(value);
    return DecodeWord32<BigIntBase::SignBits>(bitfield);
  }

  V<Word32> LoadBigIntBitfield(V<BigInt> value) {
    return __ template LoadField<Word32>(
        value, compiler::AccessBuilder::ForBigIntBitfield());
  }

  V<WordPtr> LoadBigIntDigit(V<BigInt> value, V<WordPtr> index) {
    return __ Load(value, index, LoadOp::Kind::TaggedBase(),
                   MemoryRepresentation::UintPtr(),
                   RegisterRepresentation::WordPtr(),
                   OFFSET_OF_DATA_START(BigInt), kSystemPointerSizeLog2);
  }

  // TODO(nicohartmann@): Should move the following into the assembler.
  template <typename BitField>
  V<Word32> DecodeWord32(V<Word32> word32) {
    return DecodeWord32(word32, BitField::kShift, BitField::kMask);
  }

  V<Word32> DecodeWord32(V<Word32> word32, uint32_t shift, uint32_t mask) {
    DCHECK_EQ((mask >> shift) << shift, mask);
    if ((std::numeric_limits<uint32_t>::max() >> shift) ==
        ((std::numeric_limits<uint32_t>::max() & mask) >> shift)) {
      return __ Word32ShiftRightLogical(word32, shift);
    } else {
      return __ Word32BitwiseAnd(__ Word32ShiftRightLogical(word32, shift),
                                 (mask >> shift));
    }
  }

  V<Boolean> TrueConstant() {
    DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kTrueValue));
    Handle<Object> root = isolate()->root_handle(RootIndex::kTrueValue);
    return V<Boolean>::Cast(__ HeapConstant(Handle<HeapObject>::cast(root)));
  }
  V<Boolean> FalseConstant() {
    DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kFalseValue));
    Handle<Object> root = isolate()->root_handle(RootIndex::kFalseValue);
    return V<Boolean>::Cast(__ HeapConstant(Handle<HeapObject>::cast(root)));
  }

  Isolate* isolate() { return isolate_; }

 private:
  Isolate* isolate_;
};

// TODO(nicohartmann@): We should probably provide a RETURN-helper that allows
// to leave the function in the middle.
TS_BUILTIN(BigIntEqual, TurboshaftBuiltinAssembler) {
  compiler::turboshaft::Label<Boolean> done(this);
  V<BigInt> x = Parameter<BigInt>(Descriptor::kLeft);
  V<BigInt> y = Parameter<BigInt>(Descriptor::kRight);

  GOTO_IF_NOT(__ Word32Equal(__ ReadBigIntSign(x), __ ReadBigIntSign(y)), done,
              __ FalseConstant());

  V<WordPtr> x_length = __ ReadBigIntLength(x);
  V<WordPtr> y_length = __ ReadBigIntLength(y);
  GOTO_IF_NOT(__ WordPtrEqual(x_length, y_length), done, __ FalseConstant());

  compiler::turboshaft::LoopLabel<WordPtr> loop(this);
  GOTO(loop, 0);

  // TODO(nicohartmann@): Build a nicer helper for this.
  BIND_LOOP(loop, i) {
    GOTO_IF(__ WordPtrEqual(i, x_length), done, __ TrueConstant());
    GOTO_IF_NOT(
        __ WordPtrEqual(__ LoadBigIntDigit(x, i), __ LoadBigIntDigit(y, i)),
        done, __ FalseConstant());
    GOTO(loop, __ WordPtrAdd(i, 1));
  }

  BIND(done, result);
  __ Return(result);
}

}  // namespace v8::internal

#include "src/compiler/turboshaft/undef-assembler-macros.inc"
