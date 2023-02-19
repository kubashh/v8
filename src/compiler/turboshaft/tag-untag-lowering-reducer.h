// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_TAG_UNTAG_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_TAG_UNTAG_LOWERING_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class TagUntagLoweringReducer : public Next {
  static constexpr int kSmiShiftBits = kSmiShiftSize + kSmiTagSize;

 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

  template <class... Args>
  explicit TagUntagLoweringReducer(const std::tuple<Args...>& args)
      : Next(args) {}

  OpIndex ReduceTag(OpIndex input, TagKind kind) {
    // Do shift on 32bit values if Smis are stored in the lower word.
    if constexpr (Is64() && SmiValuesAre31Bits()) {
      return ChangeTaggedInt32ToSmi(__ Word32ShiftLeft(input, kSmiShiftBits));
    } else {
      return V<Tagged>::Cast(
          __ WordPtrShiftLeft(ChangeInt32ToIntPtr(input), kSmiShiftBits));
    }
  }

  OpIndex ReduceUntag(OpIndex input, TagKind kind, RegisterRepresentation rep) {
    UNIMPLEMENTED();
  }

 private:
  V<WordPtr> ChangeInt32ToIntPtr(V<Word32> input) {
    if constexpr (Is64()) {
      return __ ChangeInt32ToInt64(input);
    } else {
      DCHECK_EQ(WordPtr::Rep, Word32::Rep);
      return V<WordPtr>::Cast(input);
    }
  }

  V<Tagged> ChangeTaggedInt32ToSmi(V<Word32> input) {
    DCHECK(SmiValuesAre31Bits());
    // In pointer compression, we smi-corrupt. Then, the upper bits are not
    // important.
    return COMPRESS_POINTERS_BOOL
               ? V<Tagged>::Cast(__ BitcastWord32ToWord64(input))
               : V<Tagged>::Cast(ChangeInt32ToIntPtr(input));
  }
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_TAG_UNTAG_LOWERING_REDUCER_H_
