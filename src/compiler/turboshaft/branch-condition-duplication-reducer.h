// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_BRANCH_CONDITION_DUPLICATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_BRANCH_CONDITION_DUPLICATION_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class ValueNumberingReducer;

// BranchConditionDuplicator makes sure that the condition inputs of branches
// are used only once. When it finds a branch whose condition has multiples
// uses, this condition is duplicated.
//
// Doing this enables the InstructionSelector to generate more efficient code
// for branches. For instance, consider this code:
//
//     if (a + b == 0) { /* some code */ }
//     if (a + b == 0) { /* more code */ }
//
// Then the generated code will be something like (using registers "ra" for "a"
// and "rb" for "b", and "rt" a temporary register):
//
//     add ra, rb  ; a + b
//     cmp ra, 0   ; (a + b) == 0
//     sete rt     ; rt = (a + b) == 0
//     cmp rt, 0   ; rt == 0
//     jz
//     ...
//     cmp rt, 0   ; rt == 0
//     jz
//
// As you can see, TurboFan materialized the == bit into a temporary register.
// However, since the "add" instruction sets the ZF flag (on x64), it can be
// used to determine wether the jump should be taken or not. The code we'd like
// to generate instead if thus:
//
//     add ra, rb
//     jnz
//     ...
//     add ra, rb
//     jnz
//
// However, this requires to generate twice the instruction "add ra, rb". Due to
// how virtual registers are assigned in TurboFan (there is a map from node ID
// to virtual registers), both "add" instructions will use the same virtual
// register as output, which will break SSA.
//
// In order to overcome this issue, BranchConditionDuplicationReducer duplicates
// branch conditions that are used more than once, so that they can be generated
// right before each branch without worrying about breaking SSA.
//
// There are some restrictions on the condition we duplicate:
//  - we only duplicate "cheap" conditions: additions, comparisons, shifts,
//    bitwise operations, but not "expensive" ones, such as multiplications or
//    divisions.
//  - duplicating conditions can extend live ranges of the inputs of the
//    conditions, so we don't do it when said inputs have a single use, in order
//    to avoid increasing register pressure too much.

template <class Next>
class BranchConditionDuplicationReducer : public Next {
#if defined(__clang__)
  // GVN breaks this optimization, since it would remove identical conditions.
  static_assert(!reducer_list_contains<typename Next::ReducerList,
                                       ValueNumberingReducer>::value);
#endif

 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

  OpIndex REDUCE_INPUT_GRAPH(Branch)(OpIndex ig_index, const BranchOp& branch) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphBranch(ig_index, branch);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    const Operation& cond = Asm().input_graph().Get(branch.condition());
    if (!ShouldDuplicate(cond)) goto no_change;

    return Next::ReduceBranch(Duplicate(cond), branch.if_true->MapToNextGraph(),
                              branch.if_false->MapToNextGraph(), branch.hint);
  }

 private:
  OpIndex Duplicate(const Operation& op) {
    switch (op.opcode) {
      case Opcode::kComparison: {
        const ComparisonOp& comp = op.Cast<ComparisonOp>();
        return Asm().ReduceComparison(Asm().MapToNewGraph(comp.left()),
                                      Asm().MapToNewGraph(comp.right()),
                                      comp.kind, comp.rep);
      }
      case Opcode::kShift: {
        const ShiftOp& shift = op.Cast<ShiftOp>();
        return Asm().ReduceShift(Asm().MapToNewGraph(shift.left()),
                                 Asm().MapToNewGraph(shift.right()), shift.kind,
                                 shift.rep);
      }
      case Opcode::kWordBinop: {
        const WordBinopOp& binop = op.Cast<WordBinopOp>();
        return Asm().ReduceWordBinop(Asm().MapToNewGraph(binop.left()),
                                     Asm().MapToNewGraph(binop.right()),
                                     binop.kind, binop.rep);
      }
      default:
        UNREACHABLE();
    }
  }

  bool ShouldDuplicate(const Operation& cond) {
    // We only allow duplication of comparisons and "cheap" binary operations
    // (cheap = not multiplication or division). The idea is that those
    // instructions set the ZF flag, and thus do not require a "== 0" to be
    // added before the branch. Duplicating other nodes, on the other hand,
    // makes little sense, because a "== 0" would need to be inserted in
    // branches anyways.
    switch (cond.opcode) {
      // This switch should be kept in sync with the one in `Duplicate`.
      case Opcode::kComparison:
      case Opcode::kShift:
        break;
      case Opcode::kWordBinop:
        switch (cond.Cast<WordBinopOp>().kind) {
          case WordBinopOp::Kind::kAdd:
          case WordBinopOp::Kind::kBitwiseAnd:
          case WordBinopOp::Kind::kBitwiseOr:
          case WordBinopOp::Kind::kBitwiseXor:
          case WordBinopOp::Kind::kSub:
            break;
          case WordBinopOp::Kind::kMul:
          case WordBinopOp::Kind::kSignedMulOverflownBits:
          case WordBinopOp::Kind::kUnsignedMulOverflownBits:
          case WordBinopOp::Kind::kSignedDiv:
          case WordBinopOp::Kind::kUnsignedDiv:
          case WordBinopOp::Kind::kSignedMod:
          case WordBinopOp::Kind::kUnsignedMod:
            // These are probably a bit too expensive to justify duplicating
            return false;
        }
        break;
      default:
        return false;
    }

    // We do not duplicate nodes if all their inputs are used a single time,
    // because this would keep those inputs alive, thus increasing register
    // pressure.
    int all_inputs_have_only_a_single_use = true;
    for (OpIndex input_idx : cond.inputs()) {
      const Operation& input = Asm().input_graph().Get(input_idx);
      if (input.saturated_use_count > 1) {
        all_inputs_have_only_a_single_use = false;
      }
    }
    if (all_inputs_have_only_a_single_use) {
      return false;
    }

    return true;
  }
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_BRANCH_CONDITION_DUPLICATION_REDUCER_H_
