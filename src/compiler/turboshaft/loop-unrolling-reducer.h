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
  // This analysis runs in O(number of block), iterating each block once, and
  // iterating blocks that are in a loop twice.
 public:
  struct LoopInfo {
    Block* start = nullptr;
    Block* end = nullptr;
    bool has_inner_loops = false;
    size_t block_count = 0;  // Number of blocks in this loop
                             // (excluding inner loops)
    size_t op_count = 0;     // Upper bound on the number of operations in this
                             // loop (excluding inner loops). This is computed
                             // using "end - begin" for each block, which is
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
 public:
  LoopUnrollingAnalyzer(Zone* phase_zone, Graph* input_graph)
      : phase_zone_(phase_zone),
        input_graph_(input_graph),
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
    auto info = loop_finder_.GetLoopInfo(loop_header);
    return !info.has_inner_loops && info.op_count < kMaxLoopSizeForUnrolling;
  }

  int GetIterationCount(Block* loop_header) const {
    DCHECK(loop_header->IsLoop());
    auto it = loop_iteration_count_.find(loop_header);
    if (it == loop_iteration_count_.end()) return 0;
    return it->second;
  }

  ZoneVector<Block*> GetLoopBody(Block* loop_header);
  Block* GetLoopHeader(Block* block) {
    return loop_finder_.GetLoopHeader(block);
  }

  static constexpr bool BinopKindIsSupported(WordBinopOp::Kind binop_kind);

 private:
  // TODO: consider increasing kMaxLoopSizeForUnrolling. For 3d-cube-SP, 450
  // required.
  static constexpr size_t kMaxLoopSizeForUnrolling = 150;
  static constexpr size_t kMaxLoopIterationsForUnrolling = 4;

  void DetectUnrollableLoops();
  int CanUnrollLoop(LoopFinder::LoopInfo info);
  int CanUnrollLoopWithCondition(const Operation& cond);
  int CanUnrollCompareBinop(uint64_t equal_cst, ComparisonOp::Kind cmp_kind,
                            uint64_t initial_input, uint64_t binop_cst,
                            WordBinopOp::Kind binop_kind);


  Zone* phase_zone_;
  Graph* input_graph_;
  LoopFinder loop_finder_;
  ZoneUnorderedMap<Block*, int> loop_iteration_count_;
};

template <class Next>
class LoopUnrollingReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

// #if defined(__clang__)
//   // LoopUnrollingReducer needs a MachineOptimizationReducer so that the loop is
//   // not generated after the last unrolled iteration.
//   static_assert(reducer_list_contains<
//                 ReducerList,
//                 MachineOptimizationReducerSignallingNanImpossible>::value);
// #endif

  OpIndex REDUCE_INPUT_GRAPH(Goto)(OpIndex ig_idx, const GotoOp& gto) {
    Block* dst = gto.destination;
    if (unrolling_ == UnrollingStatus::kNotUnrolling && dst->IsLoop() &&
        analyzer_.ShouldFullyUnrollLoop(dst)) {
      //std::cout << "Starting unrolling of loop starting at block "
      //          << dst->index() << "\n";
      FullyUnrollLoop(dst);
      return OpIndex::Invalid();
    } else if (unrolling_ == UnrollingStatus::kNotUnrolling && dst->IsLoop() &&
               analyzer_.ShouldPartiallyUnrollLoop(dst)) {
      PartiallyUnrollLoop(dst);
      return OpIndex::Invalid();
    } else if ((unrolling_ == UnrollingStatus::kUnrolling ||
                unrolling_ == UnrollingStatus::kUnrollingFirstIteration) &&
               dst == current_loop_header_) {
      // Skipping the backedge of the loop: UnrollLoop will emit a Goto to the
      // next unrolled iteration.
      //std::cout << "Skipping backedge Goto\n";
      return OpIndex::Invalid();
    } else {
      return Next::ReduceInputGraphGoto(ig_idx, gto);
    }
  }

  OpIndex REDUCE_INPUT_GRAPH(Branch)(OpIndex ig_idx, const BranchOp& branch) {
    if (unrolling_ == UnrollingStatus::kFinalizingUnrolling) {
      // We know that the branch of the final inlined header of a loop never
      // actually goes to the loop, so we can replace it by a Goto (so that the
      // non-unrolled loop doesn't get emitted).
      // We still need to figure out if we should Goto to the true or false side
      // of the BranchOp.
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
      //std::cout << "Considering removing call " << call << "\n";
      if (call.IsStackCheck(Asm().input_graph(), broker_,
                            StackCheckKind::kJSIterationBody)) {
        // When we unroll a loop, we get rid of its stack checks. (note that we
        // don't do this for the 1st folded body of partially unrolled loops so
        // that the loop keeps a stack check).
        // std::cout << "Removing stack check\n";
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
  //std::cout << "Partially unrolling loop starting at Block B"
  //          << header->index().id() << "\n";
  DCHECK_EQ(unrolling_, UnrollingStatus::kNotUnrolling);
  unrolling_ = UnrollingStatus::kUnrollingFirstIteration;

  ZoneVector<Block*> loop_body = analyzer_.GetLoopBody(header);
  current_loop_header_ = header;

  // TODO: consider having a heuristic to compute `unroll_count`.
  ScopedModification<bool> set_true(
      Asm().turn_loop_without_backedge_into_merge(), false);

  int unroll_count = 2;
  Block* output_graph_header = Asm().CloneSubGraph(loop_body, true);
  unrolling_ = UnrollingStatus::kUnrolling;
  //std::cout << "Header inlined\n";
  for (int i = 0; i < unroll_count - 1; i++) {
    Asm().CloneSubGraph(loop_body, false);
    //std::cout << "Iter " << i+1 << " inlined\n";
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
  // So far, we've skipped the backedge, which means that OptimizationPhase
  // turned the loop header into a regular Merge.
  DCHECK(output_graph_header->IsLoop());
  Block* backedge_block = Asm().current_block();
  //std::cout << "Emitting backedge a fixing phis\n";
  Asm().Goto(output_graph_header);
  FixLoopPhis(header, output_graph_header, backedge_block);

  unrolling_ = UnrollingStatus::kNotUnrolling;
  //std::cout << "Unrolling done\n";
}

template <class Next>
void LoopUnrollingReducer<Next>::FixLoopPhis(Block* input_graph_loop,
                                             Block* output_graph_loop,
                                             Block* backedge_block) {
  DCHECK(input_graph_loop->IsLoop());
  DCHECK(output_graph_loop->IsLoop());

  // The mapping InputGraphPhi -> OutputGraphPendingPhi should be retrieved from
  // `output_graph_loop`'s snapshot (the current mapping is for the latest
  // folded loop iteration, not for the loop header).
  Asm().SealAndSave();
  Asm().RestoreTemporarySnapshotAfter(output_graph_loop);
  base::SmallVector<std::pair<const PhiOp*,const OpIndex>, 16> phis;
  for (const Operation& op : Asm().input_graph().operations(
           input_graph_loop->begin(), input_graph_loop->end())) {
    if (auto* input_phi = op.TryCast<PhiOp>()) {
      OpIndex phi_index =
          Asm().template MapToNewGraph<true>(Asm().input_graph().Index(*input_phi));
      if (!phi_index.valid() || !output_graph_loop->Contains(phi_index)) {
        // Unused phis are skipped, so they are not be mapped to anything in
        // the new graph. If the phi is reduced to an operation from a
        // different block, then there is no loop phi in the current loop
        // header to take care of.
        continue;
      }
      phis.push_back({input_phi, phi_index});
    }
  }

  // The mapping for the InputGraphPhi 2nd input should however be retrieved
  // from the last block of the loop.
  Asm().CloseTemporarySnapshot();
  Asm().RestoreTemporarySnapshotAfter(backedge_block);

  for (auto [input_phi, output_phi_index] : phis) {
    Asm().FixLoopPhi(*input_phi, output_phi_index, output_graph_loop);
  }

  Asm().CloseTemporarySnapshot();
}

template <class Next>
void LoopUnrollingReducer<Next>::FullyUnrollLoop(Block* header) {
  //std::cout << "Gonna unroll loop starting at " << header->index() << "\n";
  DCHECK_EQ(unrolling_, UnrollingStatus::kNotUnrolling);
  unrolling_ = UnrollingStatus::kUnrolling;

  int iter_count = analyzer_.GetIterationCount(header);
  //std::cout << "# of iterations: " << iter_count << "\n";
  DCHECK_GT(iter_count, 0);
  ZoneVector<Block*> loop_body = analyzer_.GetLoopBody(header);
  current_loop_header_ = header;

  // std::cout << "Body: ";
  // for (Block* block : loop_body) {
  //   std::cout << block->index() << " ";
  // }
  // std::cout << "\n";

  for (int i = 0; i < iter_count; i++) {
    //std::cout << "Cloning for iter " << i << "\n";
    Asm().CloneSubGraph(loop_body, false);
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
  //std::cout << "Final cloning of header for exit\n";
  Asm().CloneAndInlineBlock(header);

  //std::cout << "Done unrolling\n";
  unrolling_ = UnrollingStatus::kNotUnrolling;
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LOOP_UNROLLING_REDUCER_H_
