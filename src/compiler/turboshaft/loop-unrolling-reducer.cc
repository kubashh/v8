// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/loop-unrolling-reducer.h"

#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operation-matching.h"

namespace v8::internal::compiler::turboshaft {

void LoopFinder::Run() {
  ZoneVector<Block*> all_loops(phase_zone_);
  for (Block& block : base::Reversed(input_graph_->blocks())) {
    if (block.IsLoop()) {
      LoopInfo info = VisitLoop(&block);
      loop_headers_.insert({&block, info});
    }
  }
}

LoopFinder::LoopInfo LoopFinder::VisitLoop(Block* header) {
  Block* backedge = header->LastPredecessor();
  DCHECK(backedge->LastOperation(*input_graph_).Is<GotoOp>());
  DCHECK_EQ(backedge->LastOperation(*input_graph_).Cast<GotoOp>().destination,
            header);

  LoopInfo info;

  queue_.clear();
  queue_.push_back(backedge);
  //std::cout << "Finding content of loop with header = " << header << "\n";
  //std::cout << "Finding loop blocks backwards from " << backedge << "\n";
  while (!queue_.empty()) {
    const Block* curr = queue_.back();
    queue_.pop_back();
    if (curr == header) continue;
    if (parent_loops_[curr->index()] != nullptr) {
      Block* curr_parent = parent_loops_[curr->index()];
      if (curr_parent == header) {
        // If {curr}'s parent is already marked as being {header}, then we've
        // already visited {curr}.
        continue;
      } else {
        // If {curr}'s parent is not {header}, then {curr} is part of an inner
        // loop. We should continue the search on the loop header: the
        // predecessors of {curr} will all be in this inner loop.
        queue_.push_back(curr_parent);
        info.has_inner_loops = true;
        continue;
      }
    }
    info.block_count++;
    info.op_count += curr->end().id() - curr->begin().id();
    parent_loops_[curr->index()] = header;
    Block* pred_start = curr->LastPredecessor();
    if (curr->IsLoop()) {
      // Skipping the backedge of inner loops since we don't want to visit inner
      // loops now (they should already have been visited).
      DCHECK_NOT_NULL(pred_start);
      pred_start = pred_start->NeighboringPredecessor();
      info.has_inner_loops = true;
    }
    for (Block* pred = pred_start; pred != nullptr;
         pred = pred->NeighboringPredecessor()) {
      queue_.push_back(pred);
    }
  }

  info.start = header;
  info.end = backedge;

  return info;
}

void LoopUnrollingAnalyzer::DetectUnrollableLoops() {
  // TODO: should special case for 1-block loops.
  //std::cout << "DetectUnrollableLoops\n";
  for (const auto& [start, info] : loop_finder_.LoopHeaders()) {
    //std::cout << "Loop starting on " << start->index() << "\n";
    if (!info.has_inner_loops) {
      //std::cout << "Has no inner loops\n";
      loop_iteration_count_.insert({start, CanUnrollLoop(info)});
    }
  }
}

int LoopUnrollingAnalyzer::CanUnrollLoop(LoopFinder::LoopInfo info) {
  Block* start = info.start;
  DCHECK(start->IsLoop());
  //std::cout << "Considering for unrolling, loop at " << start->index().id()
  //          << "\n";

  // Checking that the loop doesn't contain too many instructions.
  if (info.op_count > kMaxLoopSizeForUnrolling) {
    //std::cout << "Too large, bailing out: " << op_count << "\n";
    return 0;
  }

  // Checking that the condition for the loop can be computed statically, and
  // that the loop contains no more than kMaxLoopIterationsForFullUnrolling
  // iterations.
  const Operation& branch = start->LastOperation(*input_graph_);
  if (!branch.Is<BranchOp>()) {
    // This looks like an infinite loop, or like something weird is used to
    // decide whether to loop or not.
    //std::cout << "Doesn't start with a branch, bailing out\n";
    return 0;
  }
  const Operation& cond =
      input_graph_->Get(branch.Cast<BranchOp>().condition());
  return CanUnrollLoopWithCondition(cond);
}

int LoopUnrollingAnalyzer::CanUnrollLoopWithCondition(const Operation& cond) {
  //std::cout << "Checking cond : " << cond << "\n";

  // We try to pattern-match `for (i = cst; i cmp cst; i = i binop cst)`.

  auto try_match_loop_phi = [&](OpIndex phi_idx) -> bool {
    if (const PhiOp* phi = input_graph_->Get(phi_idx).TryCast<PhiOp>()) {
      return phi->input_count == 2;
    }
    return false;
  };
  auto try_match_int_cst = [&](OpIndex cst_idx) -> base::Optional<uint64_t> {
    if (const ConstantOp* cmp_cst =
            input_graph_->Get(cst_idx).TryCast<ConstantOp>();
        cmp_cst && (cmp_cst->kind == ConstantOp::Kind::kWord32 ||
                    cmp_cst->kind == ConstantOp::Kind::kWord64)) {
      return cmp_cst->integral();
    }
    return base::nullopt;
  };

  auto try_match_cmp = [&](const Operation& cond)
      -> base::Optional<std::tuple<CmpOp, OpIndex, uint64_t>> {
    // Tries to match `phi cmp cst`.
    CmpOp cmp_op;

    if (const ComparisonOp* cmp = cond.TryCast<ComparisonOp>()) {
      cmp_op = ComparisonKindToCmpOp(cmp->kind);
    } else if (cond.Is<EqualOp>()) {
      cmp_op = CmpOp::kEqual;
    } else {
      //std::cout << "Neither ComparisonOp nor EqualOp\n";
      return base::nullopt;
    }

    OpIndex left = cond.input(0);
    OpIndex right = cond.input(1);

    if (try_match_loop_phi(left)) {
      //std::cout << "Loop phi left\n";
      if (base::Optional<uint64_t> cst = try_match_int_cst(right)) {
        //std::cout << "Cst right\n";
        return std::tuple{cmp_op, left, *cst};
      }
    } else if (try_match_loop_phi(right)) {
      //std::cout << "Loop phi right\n";
      if (base::Optional<uint64_t> cst = try_match_int_cst(left)) {
        //std::cout << "Cmp right\n";
        cmp_op = InvertComparisonOp(cmp_op);
        return std::tuple{cmp_op, right, *cst};
      }
    }
    //std::cout << "Not phi <=> cst\n";
    return base::nullopt;
  };

  auto cmp = try_match_cmp(cond);
  if (!cmp) {
    //std::cout << "Condition not recognized\n";
    return 0;
  }
  CmpOp cmp_op = std::get<0>(*cmp);
  const PhiOp& phi = input_graph_->Get(std::get<1>(*cmp)).Cast<PhiOp>();
  uint64_t cmp_cst = std::get<2>(*cmp);

  // We have: phi(..., ...) cmp_op cmp_cst
  // eg, for (i = ...; i < 42; ...)
  if (auto phi_cst = try_match_int_cst(phi.input(0))) {
    // We have: phi(phi_cst, ...) cmp_op cmp_cst
    // eg, for (i = 0; i < 42; ...)
    // TODO: support OverflowCheckedBinop (using base::bits::SignedAddOverflow32
    // etc to perform the operations).
    if (const WordBinopOp* binop =
            input_graph_->Get(phi.input(1)).TryCast<WordBinopOp>();
        binop && BinopKindIsSupported(binop->kind)) {
      // We have: phi(phi_cst, ... op ...) cmp_op cmp_cst
      // eg, for (i = 0; i < 42; i = ... + ...)
      if (binop->left() == input_graph_->Index(phi)) {
        // We have: phi(phi_cst, phi op ...) cmp_op cmp_cst
        // eg, for (i = 0; i < 42; i = i + ...)
        if (auto binop_cst = try_match_int_cst(binop->right())) {
          // We have: phi(phi_cst, phi op binop_cst) cmp_op cmp_cst
          // eg, for (i = 0; i < 42; i = i + 2)
          int res = CanUnrollCompareBinop(cmp_cst, cmp_op, *phi_cst, *binop_cst,
                                          binop->kind);
          //std::cout << "Number of iter: " << res << "\n";
          return res;
        }
      } else if (binop->right() == input_graph_->Index(phi)) {
        // We have: phi(phi_cst, ... op phi) cmp_op cmp_cst
        // eg, for (i = 0; i < 42; i = ... + i)
        if (auto binop_cst = try_match_int_cst(binop->left())) {
          // We have: phi(phi_cst, binop_cst op phi) cmp_op cmp_cst
          // eg, for (i = 0; i < 42; i = 2 + i)
          int res = CanUnrollCompareBinop(cmp_cst, cmp_op, *phi_cst, *binop_cst,
                                       binop->kind);
          //std::cout << "Number of iter: " << res << "\n";
          return res;
        }
      }
    }
  }

  // The condition is not an operation that we support.
  //std::cout << "Bailing out\n";
  return 0;
}

constexpr bool LoopUnrollingAnalyzer::BinopKindIsSupported(
    WordBinopOp::Kind binop_kind) {
  switch (binop_kind) {
    // This list needs to be kept in sync with the `Next` function that follows.
    case WordBinopOp::Kind::kAdd:
    case WordBinopOp::Kind::kMul:
    case WordBinopOp::Kind::kSub:
    case WordBinopOp::Kind::kBitwiseAnd:
    case WordBinopOp::Kind::kBitwiseOr:
    case WordBinopOp::Kind::kBitwiseXor:
      return true;
    default:
      return false;
  }
}

namespace {
using CmpOp = LoopUnrollingAnalyzer::CmpOp;

template <class Int>
Int Next(Int val, Int incr, WordBinopOp::Kind binop_kind) {
  DCHECK(LoopUnrollingAnalyzer::BinopKindIsSupported(binop_kind));
  switch (binop_kind) {
    case WordBinopOp::Kind::kAdd:
      return val + incr;
    case WordBinopOp::Kind::kMul:
      return val * incr;
    case WordBinopOp::Kind::kSub:
      return val - incr;
    case WordBinopOp::Kind::kBitwiseAnd:
      return val & incr;
    case WordBinopOp::Kind::kBitwiseOr:
      return val | incr;
    case WordBinopOp::Kind::kBitwiseXor:
      return val ^ incr;
    default:
      UNIMPLEMENTED();
  }
}

template <class Int>
bool Cmp(Int val, Int max, CmpOp cmp_op) {
  switch (cmp_op) {
    case CmpOp::kSignedLessThan:
    case CmpOp::kUnsignedLessThan:
      return val < max;
    case CmpOp::kSignedLessThanOrEqual:
    case CmpOp::kUnsignedLessThanOrEqual:
      return val <= max;
    case CmpOp::kSignedGreaterThan:
    case CmpOp::kUnsignedGreaterThan:
      return val > max;
    case CmpOp::kSignedGreaterThanOrEqual:
    case CmpOp::kUnsignedGreaterThanOrEqual:
      return val >= max;
    case CmpOp::kEqual:
      return val == max;
  }
}

template <class Int>
int CountIter(Int init, Int max, CmpOp cmp_op, Int binop_cst,
              WordBinopOp::Kind binop_kind, int kMaxIter) {
  static_assert(std::is_integral_v<Int>);
  DCHECK(std::is_unsigned_v<Int> ==
         (cmp_op == CmpOp::kUnsignedLessThan ||
          cmp_op == CmpOp::kUnsignedLessThanOrEqual ||
          cmp_op == CmpOp::kUnsignedGreaterThan ||
          cmp_op == CmpOp::kUnsignedGreaterThanOrEqual ||
          cmp_op == CmpOp::kEqual));

  Int curr = init;
  for (int i = 0; i < kMaxIter; i++) {
    if (!Cmp(curr, max, cmp_op)) {
      //std::cout << "Number of iter: " << i << "\n";
      return i;
    }
    curr = Next(curr, binop_cst, binop_kind);
  }
  //std::cout << "More than " << kMaxIter << " iter\n";
  return 0;
}
}  // namespace


int LoopUnrollingAnalyzer::CanUnrollCompareBinop(uint64_t cmp_cst,
                                                 CmpOp cmp_op,
                                                 uint64_t initial_input,
                                                 uint64_t binop_cst,
                                                 WordBinopOp::Kind binop_kind) {
  //PrintF("cmp_cst=%ld  initial_input=%ld  binop_cst=%ld\n", cmp_cst,
  //       initial_input, binop_cst);
  switch (cmp_op) {
    case CmpOp::kSignedLessThan:
    case CmpOp::kSignedLessThanOrEqual:
    case CmpOp::kSignedGreaterThan:
    case CmpOp::kSignedGreaterThanOrEqual:
    case CmpOp::kEqual:
      return CountIter(static_cast<int64_t>(initial_input),
                       static_cast<int64_t>(cmp_cst), cmp_op,
                       static_cast<int64_t>(binop_cst), binop_kind,
                       kMaxLoopIterationsForFullUnrolling);
    case CmpOp::kUnsignedLessThan:
    case CmpOp::kUnsignedLessThanOrEqual:
    case CmpOp::kUnsignedGreaterThan:
    case CmpOp::kUnsignedGreaterThanOrEqual:
      return CountIter(initial_input, cmp_cst, cmp_op, binop_cst, binop_kind,
                       kMaxLoopIterationsForFullUnrolling);
  }
}

ZoneSet<Block*, LoopUnrollingAnalyzer::BlockCmp>
LoopUnrollingAnalyzer::GetLoopBody(Block* loop_header) {
  DCHECK(!loop_finder_.GetLoopInfo(loop_header).has_inner_loops);
  ZoneSet<Block*, BlockCmp> body(phase_zone_);
  body.insert(loop_header);

  ZoneVector<Block*> queue(phase_zone_);
  queue.push_back(loop_header->LastPredecessor());
  while (!queue.empty()) {
    Block* curr = queue.back();
    queue.pop_back();
    if (body.find(curr) != body.end()) continue;
    body.insert(curr);
    for (Block* pred = curr->LastPredecessor(); pred != nullptr;
         pred = pred->NeighboringPredecessor()) {
      if (pred == loop_header) continue;
      queue.push_back(pred);
    }
  }

  return body;
}

constexpr LoopUnrollingAnalyzer::CmpOp
LoopUnrollingAnalyzer::ComparisonKindToCmpOp(ComparisonOp::Kind kind) {
  switch (kind) {
    case ComparisonOp::Kind::kSignedLessThan:
      return CmpOp::kSignedLessThan;
    case ComparisonOp::Kind::kSignedLessThanOrEqual:
      return CmpOp::kSignedLessThanOrEqual;
    case ComparisonOp::Kind::kUnsignedLessThan:
      return CmpOp::kUnsignedLessThan;
    case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
      return CmpOp::kUnsignedLessThanOrEqual;
  }
}
constexpr LoopUnrollingAnalyzer::CmpOp
LoopUnrollingAnalyzer::InvertComparisonOp(CmpOp op) {
  switch (op) {
    case CmpOp::kEqual:
      return CmpOp::kEqual;
    case CmpOp::kSignedLessThan:
      return CmpOp::kSignedGreaterThanOrEqual;
    case CmpOp::kSignedLessThanOrEqual:
      return CmpOp::kSignedGreaterThan;
    case CmpOp::kUnsignedLessThan:
      return CmpOp::kUnsignedGreaterThanOrEqual;
    case CmpOp::kUnsignedLessThanOrEqual:
      return CmpOp::kUnsignedGreaterThan;
    case CmpOp::kSignedGreaterThan:
      return CmpOp::kSignedLessThanOrEqual;
    case CmpOp::kSignedGreaterThanOrEqual:
      return CmpOp::kSignedLessThan;
    case CmpOp::kUnsignedGreaterThan:
      return CmpOp::kUnsignedLessThanOrEqual;
    case CmpOp::kUnsignedGreaterThanOrEqual:
      return CmpOp::kUnsignedLessThan;
  }
}

}  // namespace v8::internal::compiler::turboshaft
