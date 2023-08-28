// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/loop-unrolling-reducer.h"

#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operation-matching.h"

namespace v8::internal::compiler::turboshaft {

void LoopFinder::Run() {


  //Coming back from vacation : restart from here.

  // Run: gdb --args out/x64.debug/d8 --test test/mjsunit/mjsunit.js test/mjsunit/compiler/regress-crbug-1227677.js --random-seed=910557558 --nohard-abort --verify-heap --enable-slow-asserts --testing-d8-test-runner --turboshaft --turboshaft-load-elimination --turboshaft-loop-unrolling --allow-natives-syntax --interrupt-budget=1024 --trace-turbo

  // The issue is that there is an inner loop whose backedge is _after_ the outer loop's backedge.
  // I think that I should change a bit the algorithm to make this work...


  ZoneVector<Block*> all_loops(phase_zone_);
  for (Block& block : base::Reversed(input_graph_->blocks())) {
    if (block.IsLoop()) {
      LoopInfo info = VisitLoop(&block);
      loop_headers_.insert({&block, info});
    }
  }
}


// void LoopFinder::RunOld() {
//   ZoneVector<LoopInfo> current_loops(phase_zone_);
//   for (Block& block : input_graph_->blocks()) {
//     if (!current_loops.empty() && current_loops.back().end == &block) {
//       // Just finished a loop.
//       int block_count = PropagateLoopHeaderFromBackedge(&block);
//       current_loops.back().size += block_count;
//       loop_headers_.insert({current_loops.back().start, current_loops.back()});
//       current_loops.pop_back();
//       DCHECK(!block.IsLoop());
//     } else if (block.IsLoop()) {
//       // Starting a new loop
//       if (!current_loops.empty()) {
//         // We only need to set to true the `has_inner_loops` of the last entry
//         // of {current_loops}; the previous entries should already have this
//         // field set to true, since they do have inner loops because they are
//         // not at the end of this list (which we DCHECK right below).
//         current_loops.back().has_inner_loops = true;
//       }
//       DCHECK(std::all_of(current_loops.begin(), current_loops.end(),
//                          [](LoopInfo loop) { return loop.has_inner_loops; }));

//       Block* end = block.LastPredecessor();
//       DCHECK_IMPLIES(!current_loops.empty(),
//                      current_loops.back().end->index() > end->index());
//       current_loops.push_back({&block, end, false, 0});
//     }
//   }

//   DCHECK(current_loops.empty());
// }

LoopFinder::LoopInfo LoopFinder::VisitLoop(Block* header) {
  const Block* backedge = header->LastPredecessor();
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
    info.size++;
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

  return info;
}

// Sets the "parent_loop" all of the block of the loop that ends on {backedge}
// by iterating the loop backwards from {backedge}. Returns the number of blocks
// in this loop (ignoring blocks that belong to inner loops).
int LoopFinder::PropagateLoopHeaderFromBackedge(const Block* backedge) {
  DCHECK(backedge->LastOperation(*input_graph_).Is<GotoOp>());
  Block* header =
      backedge->LastOperation(*input_graph_).Cast<GotoOp>().destination;
  DCHECK(header->IsLoop() && header->LastPredecessor() == backedge);

  int count = 0;
  queue_.clear();
  queue_.push_back(backedge);
  //std::cout << "Finding content of loop with header = " << header << "\n";
  //std::cout << "Finding loop blocks backwards from " << backedge << "\n";
  while (!queue_.empty()) {
    const Block* curr = queue_.back();
    queue_.pop_back();
    std::cout << "  curr = " << curr << "\n";
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
        continue;
      }
    }
    count++;
    parent_loops_[curr->index()] = header;
    Block* pred_start = curr->LastPredecessor();
    if (curr->IsLoop()) {
      // Skipping the backedge of inner loops since we don't want to visit inner
      // loops now (they should already have been visited).
      DCHECK_NOT_NULL(pred_start);
      pred_start = pred_start->NeighboringPredecessor();
    }
    for (Block* pred = pred_start; pred != nullptr;
         pred = pred->NeighboringPredecessor()) {
      queue_.push_back(pred);
    }
  }

  return count;
}

void LoopUnrollingAnalyzer::DetectUnrollableLoops() {
  // TODO: should special case for 1-block loops.
  //std::cout << "DetectUnrollableLoops\n";
  for (const auto& [start, info] : loop_finder_.LoopHeaders()) {
    //std::cout << "Loop starting on " << start->index() << "\n";
    if (!info.has_inner_loops) {
      //std::cout << "Has no inner loops\n";
      loop_iteration_count_.insert({start, CanUnrollLoop(start->index())});
    }
  }
}

int LoopUnrollingAnalyzer::CanUnrollLoop(BlockIndex block_idx) {
  Block& start = input_graph_->Get(block_idx);
  DCHECK(start.IsLoop());

  // Checking that the loop doesn't contain too many instructions.
  Block* end = start.LastPredecessor();
  // Note that this count is an upper bound, since some blocks between {start}
  // and {end} could not lead to {end} and thus be considered out of the loop,
  // but will still be included in this count.
  size_t op_count = end->end().id() - start.begin().id();
  if (op_count > kMaxLoopSizeForUnrolling) {
    //std::cout << "Too large, bailing out: " << op_count << "\n";
    return 0;
  }

  // Checking that the condition for the loop can be computed statically, and
  // that the loop contains no more than kMaxLoopIterationsForUnrolling
  // iterations.
  const Operation& branch = start.LastOperation(*input_graph_);
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
  if (const ComparisonOp* cmp = cond.TryCast<ComparisonOp>()) {
    //std::cout << "=> comparison\n";
    // ... < ...
    if (const ConstantOp* cmp_cst =
            input_graph_->Get(cmp->right()).TryCast<ConstantOp>();
        cmp_cst && (cmp_cst->kind == ConstantOp::Kind::kWord32 ||
                    cmp_cst->kind == ConstantOp::Kind::kWord64)) {
      //std::cout << "==> cst rhs\n";
      // ... < cmp_cst
      if (const PhiOp* phi = input_graph_->Get(cmp->left()).TryCast<PhiOp>();
          phi && phi->input_count == 2) {
        //std::cout << "===> phi lhs\n";
        // phi(..., ...) < cmp_cst
        if (const ConstantOp* phi_cst =
                input_graph_->Get(phi->input(0)).TryCast<ConstantOp>();
            phi_cst && (phi_cst->kind == ConstantOp::Kind::kWord32 ||
                        phi_cst->kind == ConstantOp::Kind::kWord64)) {
          //std::cout << "====> phi.input[0] == cst\n";
          // phi(phi_cst, ...) < cmp_cst
          if (const WordBinopOp* binop =
                  input_graph_->Get(phi->input(1)).TryCast<WordBinopOp>();
              binop && BinopKindIsSupported(binop->kind)) {
            //std::cout << "=====> phi.input[1] == add\n";
            // phi(phi_cst, ... op ...) < cmp_cst
            if (binop->left() == input_graph_->Index(*phi)) {
              //std::cout << "======> add.left = phi\n";
              // phi(phi_cst, phi op ...) < cmp_cst
              if (const ConstantOp* binop_cst =
                      input_graph_->Get(binop->right()).TryCast<ConstantOp>();
                  binop_cst && (binop_cst->kind == ConstantOp::Kind::kWord32 ||
                                binop_cst->kind == ConstantOp::Kind::kWord64)) {
                //std::cout << "======> add.right = cst\n";
                // phi(phi_cst, phi op binop_cst) < cmp_cst
                return CanUnrollCompareBinop(
                    cmp_cst->integral(), cmp->kind, phi_cst->integral(),
                    binop_cst->integral(), binop->kind);
              }  // not phi(phi_cst, phi op binop_cst) == cmp_cst
            }    // not phi(phi_cst, phi op ...) == cmp_cst
          }      // not phi(phi_cst, ... op ...) == cmp_cst
        }        // not phi(phi_cst, ...) == cmp_cst
      }          // not phi(..., ...) == cmp_cst
    }            // not ... == cmp_cst
  }              // not  ... == ...

  // else if (const ComparisonOp* cmp = cond.TryCast<ComparisonOp>()) {
  //   // TODO: support.
  // }

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
bool Cmp(Int val, Int max, ComparisonOp::Kind cmp_kind) {
  switch (cmp_kind) {
    case ComparisonOp::Kind::kSignedLessThan:
    case ComparisonOp::Kind::kUnsignedLessThan:
      return val < max;
    case ComparisonOp::Kind::kSignedLessThanOrEqual:
    case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
      return val <= max;
  }
}

template <class Int>
int CountIter(Int init, Int max, ComparisonOp::Kind cmp_kind, Int binop_cst,
              WordBinopOp::Kind binop_kind, int kMaxIter) {
  static_assert(std::is_integral_v<Int>);
  DCHECK(std::is_unsigned_v<Int> ==
         (cmp_kind == ComparisonOp::Kind::kUnsignedLessThan ||
          cmp_kind == ComparisonOp::Kind::kUnsignedLessThanOrEqual));

  Int curr = init;
  for (int i = 0; i < kMaxIter; i++) {
    if (!Cmp(curr, max, cmp_kind)) {
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
                                                 ComparisonOp::Kind cmp_kind,
                                                 uint64_t initial_input,
                                                 uint64_t binop_cst,
                                                 WordBinopOp::Kind binop_kind) {
  //PrintF("cmp_cst=%ld  initial_input=%ld  binop_cst=%ld\n", cmp_cst,
  //       initial_input, binop_cst);
  switch (cmp_kind) {
    case ComparisonOp::Kind::kSignedLessThan:
    case ComparisonOp::Kind::kSignedLessThanOrEqual:
      return CountIter(static_cast<int64_t>(initial_input),
                       static_cast<int64_t>(cmp_cst), cmp_kind,
                       static_cast<int64_t>(binop_cst), binop_kind,
                       kMaxLoopIterationsForUnrolling);
    case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
    case ComparisonOp::Kind::kUnsignedLessThan:
      return CountIter(initial_input, cmp_cst, cmp_kind, binop_cst, binop_kind,
                       kMaxLoopIterationsForUnrolling);
  }

  // switch (binop_kind) {
  //   case WordBinopOp::Kind::kAdd: {
  //     // TODO: this computation is probably wrong because of overflows.
  //     uint64_t number_of_iter = (cmp_cst - initial_input) / binop_cst;
  //     if (number_of_iter * binop_cst != cmp_cst - initial_input) {
  //       // Looks like this will loop infinitely
  //       std::cout << "Inifinte loop probably\n";
  //       return 0;
  //     }
  //     std::cout << "number_of_iter=" << number_of_iter << "\n";
  //     return number_of_iter <= kMaxLoopIterationsForUnrolling
  //                ? static_cast<int>(number_of_iter)
  //                : 0;
  //   }
  //   // TODO: handle at least kSub.
  //   default:
  //     std::cout << "kind not handled\n";
  //     return 0;
  // }
}

ZoneVector<Block*> LoopUnrollingAnalyzer::GetLoopBody(Block* loop_header) {
  LoopFinder::LoopInfo info = loop_finder_.GetLoopInfo(loop_header);
  DCHECK(!info.has_inner_loops);
  auto cmp = [](Block* a, Block* b) { return a->index() < b->index(); };
  ZoneSet<Block*, decltype(cmp)> body(phase_zone_);
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

  // TODO(dmercadier): consider returning a Set rather than a Vector.
  ZoneVector<Block*> body_vec(phase_zone_);
  body_vec.reserve(info.size);
  for (Block* block : body) {
    body_vec.push_back(block);
  }

  return body_vec;
}

// void LoopUnrollingAnalyzer::GetLoopBody(ZoneUnorderedSet<Block*>& loop_body,
//                                         BlockIndex loop_header) {
//   loop_body.clear();

//   const Block& start = input_graph_->Get(loop_header);
//   DCHECK(start.IsLoop());
//   Block* end = start.LastPredecessor();

//   base::SmallVector<Block*, 16> queue;
//   queue.push_back(end);
//   while (!queue.empty()) {
//     Block* curr = queue.back();
//     queue.pop_back();
//     if (loop_body.find(curr) == loop_body.end()) {
//       loop_body.insert(curr);
//       for (Block* pred = curr->LastPredecessor(); pred != nullptr;
//            pred = pred->NeighboringPredecessor()) {
//         if (pred != &start && loop_body.find(pred) == loop_body.end()) {
//           queue.push_back(pred);
//         }
//       }
//     }
//   }
// }

}  // namespace v8::internal::compiler::turboshaft
