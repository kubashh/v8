// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LOOP_UNROLLING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_LOOP_UNROLLING_REDUCER_H_

#include "src/base/logging.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/optimization-phase.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

class LoopFinder {
  // This analyzer finds which loop each Block of a graph belongs to, and
  // computes a list of all of the loops headers.
  //
  // A block is considered to "belong to a loop" if there is a forward-path (ie,
  // without taking backedges) from this block to the backedge of the loop.
  //
  // This analysis runs in O(number of blocks), iterating each block once, and
  // iterating blocks that are in a loop twice.
  //
  // Implementation:
  // LoopFinder::Run walks the blocks of the graph backwards, and when it
  // reaches a LoopHeader, it calls LoopFinder::VisitLoop.
  // LoopFinder::VisitLoop iterates all of the blocks of the loop backwards,
  // starting from the backedge, and stopping upon reaching the loop header. It
  // marks the blocks that don't have a `parent_loops_` set as being part of the
  // current loop (= sets their `parent_loops_` to the current loop header). If
  // it finds a block that already has a `parent_loops_` set, it means that this
  // loop contains an inner loop, so we skip this inner block as set the
  // `has_inner_loops` bit.
  //
  // By iterating the blocks backwards in Run, we are guaranteed that inner
  // loops are visited before their outer loops. Walking the graph forward
  // doesn't work quite as nicely:
  //  - When seeing loop headers for the 1st time, we wouldn't have visited
  //    their inner loops yet.
  //  - If we decided to still iterate forward but to call VisitLoop when
  //    reaching their backedge rather than their header, it would work in most
  //    cases but not all, since the backedge of an outer loop can have a
  //    BlockIndex that is smaller than the one of an inner loop.
 public:
  struct LoopInfo {
    Block* start = nullptr;
    Block* end = nullptr;
    bool has_inner_loops = false;
    size_t block_count = 0;  // Number of blocks in this loop
                             // (excluding inner loops)
    size_t op_count = 0;     // Upper bound on the number of operations in this
                             // loop (excluding inner loops). This is computed
                             // using "end - begin" for each block, which can be
                             // more than the number of operations when some
                             // operations are large (like CallOp and
                             // FrameStateOp typically).
  };
  LoopFinder(Zone* phase_zone, Graph* input_graph)
      : phase_zone_(phase_zone),
        input_graph_(input_graph),
        parent_loops_(input_graph->block_count(), nullptr, phase_zone),
        loop_headers_(phase_zone),
        queue_(phase_zone) {}

  void Run();
  const ZoneUnorderedMap<Block*, LoopInfo> LoopHeaders() const {
    return loop_headers_;
  }
  Block* GetLoopHeader(Block* block) { return parent_loops_[block->index()]; }
  LoopInfo GetLoopInfo(Block* block) const {
    DCHECK(block->IsLoop());
    auto it = loop_headers_.find(block);
    DCHECK_NE(it, loop_headers_.end());
    return it->second;
  }

 private:
  LoopInfo VisitLoop(Block* header);

  Zone* phase_zone_;
  Graph* input_graph_;
  FixedBlockSidetable<Block*> parent_loops_;
  ZoneUnorderedMap<Block*, LoopInfo> loop_headers_;

  // {queue_} is used in `VisitLoop`, but is declared as a class variable to
  // reuse memory.
  ZoneVector<const Block*> queue_;
};

class LoopUnrollingAnalyzer {
  // LoopUnrollingAnalyzer analyzes the loops of the graph, and in particular
  // tries to figure out if some inner loops have a fixed (and known) number of
  // iterations. In particular, it tries to pattern match loops like
  //
  //    for (let i = 0; i < 4; i++) { ... }
  //
  // where `i++` could alternatively be pretty much any WordBinopOp or
  // OverflowCheckedBinopOp, and `i < 4` could be any ComparisonOp or EqualOp.
  // Such loops, if small enough, could be fully unrolled.
  //
  // Loops that don't have statically-known bounds could still be partially
  // unrolled if they are small enough.
 public:
  LoopUnrollingAnalyzer(Zone* phase_zone, Graph* input_graph)
      : phase_zone_(phase_zone),
        input_graph_(input_graph),
        matcher_(*input_graph),
        loop_finder_(phase_zone, input_graph),
        loop_iteration_count_(phase_zone) {
    loop_finder_.Run();
    DetectUnrollableLoops();
  }

  bool ShouldFullyUnrollLoop(Block* loop_header) const {
    DCHECK(loop_header->IsLoop());
    DCHECK_IMPLIES(GetIterationCount(loop_header) > 0,
                   !loop_finder_.GetLoopInfo(loop_header).has_inner_loops);
    return GetIterationCount(loop_header) > 0;
  }

  bool ShouldPartiallyUnrollLoop(Block* loop_header) const {
    DCHECK(loop_header->IsLoop());
    auto info = loop_finder_.GetLoopInfo(loop_header);
    return !info.has_inner_loops && info.op_count < kMaxLoopSizeForUnrolling;
  }

  int GetIterationCount(Block* loop_header) const {
    DCHECK(loop_header->IsLoop());
    auto it = loop_iteration_count_.find(loop_header);
    if (it == loop_iteration_count_.end()) return 0;
    return it->second;
  }

  struct BlockCmp {
    bool operator()(Block* a, Block* b) const {
      return a->index().id() < b->index().id();
    }
  };
  ZoneSet<Block*, BlockCmp> GetLoopBody(Block* loop_header);

  Block* GetLoopHeader(Block* block) {
    return loop_finder_.GetLoopHeader(block);
  }

  enum class CmpOp {
    kEqual,
    kSignedLessThan,
    kSignedLessThanOrEqual,
    kUnsignedLessThan,
    kUnsignedLessThanOrEqual,
    kSignedGreaterThan,
    kSignedGreaterThanOrEqual,
    kUnsignedGreaterThan,
    kUnsignedGreaterThanOrEqual,

  };
  static constexpr CmpOp ComparisonKindToCmpOp(ComparisonOp::Kind kind);
  static constexpr CmpOp InvertComparisonOp(CmpOp op);
  enum class BinOp {
    kAdd,
    kMul,
    kSub,
    kBitwiseAnd,
    kBitwiseOr,
    kBitwiseXor,
    kOverflowCheckedAdd,
    kOverflowCheckedMul,
    kOverflowCheckedSub
  };
  static constexpr BinOp BinopFromWordBinopKind(WordBinopOp::Kind kind);
  static constexpr BinOp BinopFromOverflowCheckedBinopKind(
      OverflowCheckedBinopOp::Kind kind);
  static constexpr bool BinopKindIsSupported(WordBinopOp::Kind binop_kind);

  // TODO(dmercadier): consider tweaking these value for a better size-speed
  // trade-off. In particular, having the number of iterations to unroll be a
  // function of the loop's size and a MaxLoopSize could make sense.
  static constexpr size_t kMaxLoopSizeForUnrolling = 150;
  static constexpr size_t kMaxLoopIterationsForFullUnrolling = 4;
  static constexpr size_t kPartialUnrollingCount = 4;

 private:
  void DetectUnrollableLoops();
  int CanUnrollLoop(LoopFinder::LoopInfo info);
  int CanUnrollLoopWithCondition(const Operation& cond);
  int CanUnrollCompareBinop(uint64_t equal_cst, CmpOp cmp_op,
                            uint64_t initial_input, uint64_t binop_cst,
                            BinOp binop_op, WordRepresentation binop_rep);

  Zone* phase_zone_;
  Graph* input_graph_;
  OperationMatcher matcher_;
  LoopFinder loop_finder_;
  ZoneUnorderedMap<Block*, int> loop_iteration_count_;
};

template <class Next>
class LoopUnrollingReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

  OpIndex REDUCE_INPUT_GRAPH(Goto)(OpIndex ig_idx, const GotoOp& gto) {
    // We trigger unrolling when reaching the GotoOp that jumps to the loop
    // header (note that loop headers only have 2 predecessor, including the
    // backedge).
    Block* dst = gto.destination;
    if (unrolling_ == UnrollingStatus::kNotUnrolling && dst->IsLoop() &&
        analyzer_.ShouldFullyUnrollLoop(dst)) {
      FullyUnrollLoop(dst);
      return OpIndex::Invalid();
    } else if (unrolling_ == UnrollingStatus::kNotUnrolling && dst->IsLoop() &&
               analyzer_.ShouldPartiallyUnrollLoop(dst)) {
      PartiallyUnrollLoop(dst);
      return OpIndex::Invalid();
    } else if ((unrolling_ == UnrollingStatus::kUnrolling ||
                unrolling_ == UnrollingStatus::kUnrollingFirstIteration) &&
               dst == current_loop_header_) {
      // Skipping the backedge of the loop: FullyUnrollLoop and
      // PartiallyUnrollLoop will emit a Goto to the next unrolled iteration.
      return OpIndex::Invalid();
    } else {
      return Next::ReduceInputGraphGoto(ig_idx, gto);
    }
  }

  OpIndex REDUCE_INPUT_GRAPH(Branch)(OpIndex ig_idx, const BranchOp& branch) {
    if (unrolling_ == UnrollingStatus::kFinalizingUnrolling) {
      // We know that the branch of the final inlined header of a fully unrolled
      // loop never actually goes to the loop, so we can replace it by a Goto
      // (so that the non-unrolled loop doesn't get emitted). We still need to
      // figure out if we should Goto to the true or false side of the BranchOp.
      const Block* header = Asm().current_block()->OriginForBlockEnd();
      bool is_true_in_loop = analyzer_.GetLoopHeader(branch.if_true) == header;
      bool is_false_in_loop =
          analyzer_.GetLoopHeader(branch.if_false) == header;

      if (is_true_in_loop && !is_false_in_loop) {
        Asm().Goto(Asm().MapToNewGraph(branch.if_false));
        return OpIndex::Invalid();
      } else if (is_false_in_loop && !is_true_in_loop) {
        Asm().Goto(Asm().MapToNewGraph(branch.if_true));
        return OpIndex::Invalid();
      } else {
        // Both the true and false destinations of this block are in the loop,
        // which means that the exit of the loop is later down the graph. We
        // thus still emit the branch, which will lead to the loop being emitted
        // (unless some other reducers in the stack manage to get rid of the
        // loop).
        DCHECK(is_true_in_loop && is_false_in_loop);
      }
    }
    return Next::ReduceInputGraphBranch(ig_idx, branch);
  }

  OpIndex REDUCE_INPUT_GRAPH(Call)(OpIndex ig_idx, const CallOp& call) {
    if (unrolling_ == UnrollingStatus::kUnrolling) {
      if (call.IsStackCheck(Asm().input_graph(), broker_,
                            StackCheckKind::kJSIterationBody)) {
        // When we unroll a loop, we get rid of its stack checks. (note that we
        // don't do this for the 1st folded body of partially unrolled loops so
        // that the loop keeps a stack check).
        DCHECK_NE(unrolling_, UnrollingStatus::kUnrollingFirstIteration);
        return OpIndex::Invalid();
      }
    }
    return Next::ReduceInputGraphCall(ig_idx, call);
  }

 private:
  enum class UnrollingStatus {
    kNotUnrolling,             // Not currently unrolling a loop.
    kUnrollingFirstIteration,  // Currently on the 1st iteration of a partially
                               // unrolled loop.
    kUnrolling,                // Currently unrolling a loop.
    kFinalizingUnrolling  // Unrolling is finished and we are currently emitting
                          // the header a last time, and should change its final
                          // Branch into a Goto.
  };
  void FullyUnrollLoop(Block* header);
  void PartiallyUnrollLoop(Block* header);
  void FixLoopPhis(Block* input_graph_loop, Block* output_graph_loop,
                   Block* backedge_block);

  ZoneUnorderedSet<Block*> loop_body_{Asm().phase_zone()};
  LoopUnrollingAnalyzer analyzer_{Asm().phase_zone(),
                                  &Asm().modifiable_input_graph()};
  // {unrolling_} is true if a loop is currently being unrolled.
  UnrollingStatus unrolling_ = UnrollingStatus::kNotUnrolling;
  void* current_loop_header_ = nullptr;
  JSHeapBroker* broker_ = PipelineData::Get().broker();
};

template <class Next>
void LoopUnrollingReducer<Next>::PartiallyUnrollLoop(Block* header) {
  DCHECK_EQ(unrolling_, UnrollingStatus::kNotUnrolling);
  // When unrolling the 1st iteration,

  auto loop_body = analyzer_.GetLoopBody(header);
  current_loop_header_ = header;

  int unroll_count = LoopUnrollingAnalyzer::kPartialUnrollingCount;

  ScopedModification<bool> set_true(
      Asm().turn_loop_without_backedge_into_merge(), false);

  // Emitting the 1st iteration of the loop (with a proper loop header). We set
  // UnrollingStatus to kUnrollingFirstIteration instead of kUnrolling so that
  // the stack check still gets emitted. For the subsequent iterations, we'll
  // set it to kUnrolling so that stack checks are skipped.
  unrolling_ = UnrollingStatus::kUnrollingFirstIteration;
  Block* output_graph_header =
      Asm().CloneSubGraph(loop_body, /* keep_loop_kinds */ true);

  // Emitting the subsequent folded iterations. We set `unrolling_` to
  // kUnrolling so that stack checks are skipped.
  unrolling_ = UnrollingStatus::kUnrolling;
  for (int i = 0; i < unroll_count - 1; i++) {
    Asm().CloneSubGraph(loop_body, /* keep_loop_kinds */ false);
    if (Asm().generating_unreachable_operations()) {
      // By unrolling the loop, we realized that it was actually exiting early
      // (probably because a Branch inside the loop was using a loop Phi in a
      // condition, and unrolling showed that this loop Phi became true or
      // false), and that lasts iterations were unreachable. We thus don't both
      // unrolling the next iterations of the loop.
      unrolling_ = UnrollingStatus::kNotUnrolling;
      Asm().FinalizeLoop(output_graph_header);
      return;
    }
  }

  // ReduceInputGraphGoto ignores backedge Gotos while kUnrolling is true, so we
  // now emit the final backedge Goto.
  unrolling_ = UnrollingStatus::kFinalizingUnrolling;
  DCHECK(output_graph_header->IsLoop());
  Block* backedge_block = Asm().current_block();
  Asm().Goto(output_graph_header);
  // We use a custom `FixLoopPhis` because the mapping from old->new is a bit
  // "messed up" by having emitted multiple times the same block. See the
  // comments in `FixLoopPhis` for more details.
  FixLoopPhis(header, output_graph_header, backedge_block);

  unrolling_ = UnrollingStatus::kNotUnrolling;
}

template <class Next>
void LoopUnrollingReducer<Next>::FixLoopPhis(Block* input_graph_loop,
                                             Block* output_graph_loop,
                                             Block* backedge_block) {
  // FixLoopPhis for partially unrolled loops is a bit tricky: the mapping from
  // input Loop Phis to output Loop Phis is in the Variable Snapshot of the
  // header (`output_graph_loop`), but the mapping from the 2nd input of the
  // input graph loop phis to the 2nd input of the output graph loop phis is in
  // the snapshot of the backedge (`backedge_block`).
  // We thus start by retrieving the mapping for the 2nd inputs in the current
  // snapshot (which is still opened). We then close it and restore the snapshot
  // of the loop header where we get the Loop Phis mappings. Performance-wise,
  // this is pretty good since anyways, once the loop has been processed, the
  // next block to process should be the outside-of-the-loop successor of the
  // loop header, which would restore the loop header's snapshot.
  DCHECK(input_graph_loop->IsLoop());
  DCHECK(output_graph_loop->IsLoop());

  // Retrieving the mapping from input graph loop phis' 2nd inputs to new graph.
  base::SmallVector<std::pair<const PhiOp*, const OpIndex>, 16> phis_2nd_input;
  for (const Operation& op : Asm().input_graph().operations(
           input_graph_loop->begin(), input_graph_loop->end())) {
    if (auto* input_phi = op.TryCast<PhiOp>()) {
      DCHECK_EQ(input_phi->input_count, 2);
      phis_2nd_input.push_back(
          {input_phi, Asm().template MapToNewGraph<true>(input_phi->input(1))});
    }
  }

  // The mapping from input graph loop phis to output graph loop phis is not in
  // the current snapshot but rather in the loop header's snapshot (since it was
  // since then inlined, which overwrote these mappings). We thus now need to
  // restore this snapshot.
  // We start by saving the current snapshot still.
  Asm().SealAndSaveVariableSnapshot();
  // Restoring the snapshot after the loop header.
  Asm().RestoreTemporaryVariableSnapshotAfter(output_graph_loop);
  for (auto [input_phi, input_2] : phis_2nd_input) {
    OpIndex output_phi_index = Asm().template MapToNewGraph<true>(
        Asm().input_graph().Index(*input_phi));
    if (output_phi_index.valid() &&
        output_graph_loop->Contains(output_phi_index)) {
      DCHECK_IMPLIES(Asm()
                         .output_graph()
                         .Get(output_phi_index)
                         .template Is<PendingLoopPhiOp>(),
                     input_2.valid());
      Asm().FixLoopPhi(*input_phi, output_phi_index, output_graph_loop);
    }
  }
  // Closing the temporary snapshot
  Asm().CloseTemporaryVariableSnapshot();
}

template <class Next>
void LoopUnrollingReducer<Next>::FullyUnrollLoop(Block* header) {
  DCHECK_EQ(unrolling_, UnrollingStatus::kNotUnrolling);

  int iter_count = analyzer_.GetIterationCount(header);
  DCHECK_GT(iter_count, 0);

  auto loop_body = analyzer_.GetLoopBody(header);
  current_loop_header_ = header;

  unrolling_ = UnrollingStatus::kUnrolling;
  for (int i = 0; i < iter_count; i++) {
    Asm().CloneSubGraph(loop_body, /* keep_loop_kinds */ false);
    if (Asm().generating_unreachable_operations()) {
      // By unrolling the loop, we realized that it was actually exiting early
      // (probably because a Branch inside the loop was using a loop Phi in a
      // condition, and unrolling showed that this loop Phi became true or
      // false), and that lasts iterations were unreachable. We thus don't both
      // unrolling the next iterations of the loop.
      unrolling_ = UnrollingStatus::kNotUnrolling;
      return;
    }
  }

  // The loop actually finishes on the header rather than its last block. We
  // thus inline the header, and we'll replace its final BranchOp by a GotoOp to
  // outside of the loop.
  unrolling_ = UnrollingStatus::kFinalizingUnrolling;
  Asm().CloneAndInlineBlock(header);

  unrolling_ = UnrollingStatus::kNotUnrolling;
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LOOP_UNROLLING_REDUCER_H_
