// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_SELECT_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_SELECT_LOWERING_REDUCER_H_

#include <fstream>

#include "src/base/container-utils.h"
#include "src/base/vector.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// Lowers Select operations to diamonds.
//
// A Select is conceptually somewhat similar to a ternary if:
//
//       res = Select(cond, val_true, val_false)
//
// means:
//
//       res = cond ? val_true : val_false
//
// SelectLoweringReducer lowers such operations into:
//
//     if (cond) {
//         res = val_true
//     } else {
//         res = val_false
//     }

template <class Next>
class SelectLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(SelectLowering)

  OpIndex REDUCE(Select)(OpIndex cond, OpIndex vtrue, OpIndex vfalse,
                         RegisterRepresentation rep, BranchHint hint,
                         SelectOp::Implementation implem) {
    if (implem == SelectOp::Implementation::kCMove) {
      // We do not lower Select operations that should be implemented with
      // CMove.
      return Next::ReduceSelect(cond, vtrue, vfalse, rep, hint, implem);
    }

    // #ifdef V8_TARGET_ARCH_X64
    //     if (rep.IsWord()) {
    //       return Next::ReduceSelect(cond, vtrue, vfalse, rep, hint, implem);
    //     }
    // #endif

    Variable result = __ NewLoopInvariantVariable(rep);
    IF (cond) {
      __ SetVariable(result, vtrue);
    } ELSE {
      __ SetVariable(result, vfalse);
    }

    return __ GetVariable(result);
  }
};

template <typename Next>
class SelectDetectionReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(SelectDetectionReducer)

  SelectDetectionReducer() {
    //    trace_.open("select-detection-reducer.trace", std::ios::out |
    //    std::ios::app); DCHECK(trace_.is_open());
  }

  const SelectOp* IsSelectWithConstant(OpIndex index) {
    const Operation& op = graph_.Get(index);
    const SelectOp* select = op.TryCast<SelectOp>();
    if (select == nullptr) return nullptr;
    const Operation& left_input = graph_.Get(select->vtrue());
    const Operation& right_input = graph_.Get(select->vfalse());
    if (!left_input.Is<ConstantOp>() && !right_input.Is<ConstantOp>())
      return nullptr;
    return select;
  }

  OpIndex REDUCE(TaggedBitcast)(OpIndex input, RegisterRepresentation from,
                                RegisterRepresentation to,
                                TaggedBitcastOp::Kind kind) {
    if (const SelectOp* select = IsSelectWithConstant(input)) {
      DCHECK_EQ(select->rep, from);
      if (from == RegisterRepresentation::Tagged() && to.IsWord()) {
        // Tagged -> Untagged
        OpIndex vtrue = __ TaggedBitcast(select->vtrue(), from, to, kind);
        OpIndex vfalse = __ TaggedBitcast(select->vfalse(), from, to, kind);
        return __ Select(select->cond(), vtrue, vfalse, to, select->hint,
                         select->implem);
      } else if (from.IsWord() && to == RegisterRepresentation::Tagged()) {
        // Untagged -> Smi
        if (kind == any_of(TaggedBitcastOp::Kind::kSmi,
                           TaggedBitcastOp::Kind::kTagAndSmiBits)) {
          OpIndex vtrue = __ TaggedBitcast(select->vtrue(), from, to, kind);
          OpIndex vfalse = __ TaggedBitcast(select->vfalse(), from, to, kind);
          return __ Select(select->cond(), vtrue, vfalse, to, select->hint,
                           select->implem);
        }
      }
    }
    return Next::ReduceTaggedBitcast(input, from, to, kind);
  }

  OpIndex REDUCE(Change)(OpIndex input, ChangeOp::Kind kind,
                         ChangeOp::Assumption assumption,
                         RegisterRepresentation from,
                         RegisterRepresentation to) {
    if (const SelectOp* select = IsSelectWithConstant(input)) {
      DCHECK_EQ(select->rep, from);
      if (kind == ChangeOp::Kind::kTruncate) {
        if (from == RegisterRepresentation::Word64() &&
            to == RegisterRepresentation::Word32()) {
          OpIndex vtrue =
              __ Change(select->vtrue(), kind, assumption, from, to);
          OpIndex vfalse =
              __ Change(select->vfalse(), kind, assumption, from, to);
          return __ Select(select->cond(), vtrue, vfalse, to, select->hint,
                           select->implem);
        }
      } else if (kind == ChangeOp::Kind::kSignExtend) {
        if (from == RegisterRepresentation::Word32() &&
            to == RegisterRepresentation::Word64()) {
          OpIndex vtrue =
              __ Change(select->vtrue(), kind, assumption, from, to);
          OpIndex vfalse =
              __ Change(select->vfalse(), kind, assumption, from, to);
          return __ Select(select->cond(), vtrue, vfalse, to, select->hint,
                           select->implem);
        }
      }
    }
    return Next::ReduceChange(input, kind, assumption, from, to);
  }

  OpIndex REDUCE(WordBinop)(OpIndex left, OpIndex right, WordBinopOp::Kind kind,
                            WordRepresentation rep) {
    OpIndex inputs[] = {left, right};
    for (int select_index = 0; select_index < 2; ++select_index) {
      const SelectOp* select = IsSelectWithConstant(inputs[select_index]);
      if (select == nullptr) continue;
      DCHECK_EQ(select->rep, rep);
      const int constant_index = 1 - select_index;
      const ConstantOp* constant =
          graph_.Get(inputs[constant_index]).TryCast<ConstantOp>();
      if (constant == nullptr) continue;
      DCHECK_EQ(constant->kind,
                any_of(ConstantOp::Kind::kWord32, ConstantOp::Kind::kWord64));
      // WordBinop(Select(c, t, f), k) ==> Select(c, WordBinop(t, k),
      // WordBinop(f, k)) WordBinop(k, Select(c, t, f)) ==> Select(c,
      // WordBinop(k, t), WordBinop(k, f))
      inputs[select_index] = select->vtrue();
      OpIndex vtrue = __ WordBinop(inputs[0], inputs[1], kind, rep);
      inputs[select_index] = select->vfalse();
      OpIndex vfalse = __ WordBinop(inputs[0], inputs[1], kind, rep);
      return __ Select(select->cond(), vtrue, vfalse, select->rep, select->hint,
                       select->implem);
    }
    return Next::ReduceWordBinop(left, right, kind, rep);
  }

  OpIndex REDUCE(Phi)(base::Vector<const OpIndex> inputs,
                      RegisterRepresentation rep) {
    if (inputs.size() == 2 && (rep.IsWord() || rep.IsTaggedOrCompressed())) {
      DCHECK_EQ(__ current_block()->PredecessorCount(), 2);
      Block* predecessor1 = __ current_block() -> LastPredecessor();
      Block* predecessor0 = predecessor1->NeighboringPredecessor();
      bool negated;
      Block* diamond_root =
          FindDiamondRoot(predecessor0, predecessor1, negated);
      if (diamond_root) {
        OpIndex values[] = {
            inputs[0],
            inputs[1],
        };
        if (negated) std::swap(values[0], values[1]);
        bool can_optimize = true;
        std::function<OpIndex(OpIndex)> builder[2] = {
            [](OpIndex i) { return i; }, [](OpIndex i) { return i; }};
        for (int i = 0; i < 2; ++i) {
          Block* block = &graph_.Get(graph_.BlockIndexOf(values[i]));
          if (block->GetCommonDominator(diamond_root) != block &&
              !IsConstantLike(values[i], builder[i])) {
            can_optimize = false;
            break;
          }
        }
        if (can_optimize) {
          for (int i = 0; i < 2; ++i) values[i] = builder[i](values[i]);
          const BranchOp& branch =
              diamond_root->LastOperation(graph_).Cast<BranchOp>();
          //        const Operation& condition = graph_.Get(branch.condition());
          trace_ << "1,\n";
          //          std::cout << "SELECT DETECTED!" << std::endl;
          return __ Select(branch.condition(), values[0], values[1], rep,
                           branch.hint, SelectOp::Implementation::kBranch);
        }
      }
    }

    return Next::ReducePhi(inputs, rep);
  }

 private:
  bool IsConstantLike(OpIndex index, std::function<OpIndex(OpIndex)>& builder) {
    const Operation& op = graph_.Get(index);
    switch (op.opcode) {
      case Opcode::kConstant: {
        builder = [&](OpIndex) -> OpIndex {
          return __ Constant(op.Cast<ConstantOp>().kind,
                             op.Cast<ConstantOp>().storage);
        };
        return true;
      }
      default:
        return false;
    }
  }

  Block* FindDiamondRoot(Block* predecessor0, Block* predecessor1,
                         bool& negated) {
    std::set<const Block*> candidates;
    // Follow left side.
    while (true) {
      if (const BranchOp* branch =
              predecessor0->LastOperation(graph_).TryCast<BranchOp>()) {
        candidates.insert(predecessor0);
      }
      if (predecessor0->PredecessorCount() != 1) break;
      predecessor0 = predecessor0->LastPredecessor();
    }
    if (candidates.empty()) return nullptr;

    // Follow right side.
    const Block* temp = nullptr;
    while (true) {
      if (base::contains(candidates, predecessor1)) {
        DCHECK_NOT_NULL(temp);
        const BranchOp& branch =
            predecessor1->LastOperation(graph_).Cast<BranchOp>();
        negated = branch.if_true == temp;
        return predecessor1;
      }
      if (predecessor1->PredecessorCount() != 1) return nullptr;
      temp = predecessor1;
      predecessor1 = predecessor1->LastPredecessor();
    }
  }

  Graph& graph_ = __ output_graph();
  std::ofstream trace_;
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_SELECT_LOWERING_REDUCER_H_
