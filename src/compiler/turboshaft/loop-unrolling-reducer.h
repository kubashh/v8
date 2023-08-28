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
    int size = 0;  // Number of blocks in this loop (excluding inner loops)
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
  LoopInfo GetLoopInfo(Block* block) {
    DCHECK(block->IsLoop());
    auto it = loop_headers_.find(block);
    DCHECK_NE(it, loop_headers_.end());
    return it->second;
  }

 private:
  LoopInfo VisitLoop(Block* header);
  int PropagateLoopHeaderFromBackedge(const Block* block);

  Zone* phase_zone_;
  Graph* input_graph_;
  FixedBlockSidetable<Block*> parent_loops_;
  ZoneUnorderedMap<Block*, LoopInfo> loop_headers_;

  // {queue_} is used in `PropagateLoopHeaderFromBackedge`, but is declared as a
  // class variable to reuse memory.
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

  // void GetLoopBody(ZoneUnorderedSet<Block*>& loop_body, BlockIndex
  // loop_header);
  bool ShouldUnrollLoop(Block* loop_header) const {
    DCHECK(loop_header->IsLoop());
    return GetIterationCount(loop_header) > 0;
  }

  int GetIterationCount(Block* loop_header) const {
    DCHECK(loop_header->IsLoop());
    auto it = loop_iteration_count_.find(loop_header);
    if (it == loop_iteration_count_.end()) return 0;
    return it->second;
  }

  ZoneVector<Block*> GetLoopBody(Block* loop_header);

  static constexpr bool BinopKindIsSupported(WordBinopOp::Kind binop_kind);

 private:
  // TODO: consider increasing kMaxLoopSizeForUnrolling. For 3d-cube-SP, 450
  // required.
  static constexpr size_t kMaxLoopSizeForUnrolling = 150;
  static constexpr size_t kMaxLoopIterationsForUnrolling = 4;

  void DetectUnrollableLoops();
  int CanUnrollLoop(BlockIndex block_idx);
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

#if defined(__clang__)
  // LoopUnrollingReducer needs a MachineOptimizationReducer so that the loop is
  // not generated after the last unrolled iteration.
  static_assert(reducer_list_contains<
                ReducerList,
                MachineOptimizationReducerSignallingNanImpossible>::value);
#endif

  OpIndex REDUCE_INPUT_GRAPH(Goto)(OpIndex ig_idx, const GotoOp& gto) {
    Block* dst = gto.destination;
    if (unrolling_ == UnrollingStatus::kNotUnrolling && dst->IsLoop() &&
        analyzer_.ShouldUnrollLoop(dst)) {
      //std::cout << "Starting unrolling of loop starting at block "
      //          << dst->index() << "\n";
      UnrollLoop(dst);
      return OpIndex::Invalid();
    } else if (unrolling_ == UnrollingStatus::kUnrolling &&
               dst == current_loop_header_) {
      // Skipping the backedge of the loop: UnrollLoop will emit a Goto to the
      // next unrolled iteration.
      //std::cout << "Skipping backedge Goto\n";
      return OpIndex::Invalid();
    } else {
      return Next::ReduceInputGraphGoto(ig_idx, gto);
    }
  }

  OpIndex REDUCE_INPUT_GRAPH(Call)(OpIndex ig_idx, const CallOp& call) {
    if (unrolling_ == UnrollingStatus::kUnrolling) {
      //std::cout << "Considering removing call " << call << "\n";
      if (call.IsStackCheck(Asm().input_graph(), broker_,
                            StackCheckKind::kJSIterationBody)) {
        // When we unroll a loop, we get rid of its stack checks.
        //std::cout << "Removing stack check\n";
        return OpIndex::Invalid();
      } else {
        //std::cout
        //    << "Not removing stack check because not a stack check actually\n";
      }
    }
    // if (unrolling_ == UnrollingStatus::kUnrolling &&
    //     call.IsStackCheck(Asm().input_graph(), broker_,
    //                       StackCheckKind::kJSIterationBody)) {
    //   // When we unroll a loop, we get rid of its stack checks.
    //   return OpIndex::Invalid();
    // }
    return Next::ReduceInputGraphCall(ig_idx, call);
  }

 private:
  enum class UnrollingStatus {
    kNotUnrolling,        // Not currently unrolling a loop.
    kUnrolling,           // Currently unrolling a loop.
    kFinalizingUnrolling  // Unrolling is finished and we are currently emitting
                          // the header a last time, and should change its final
                          // Branch into a Goto.
  };
  void UnrollLoop(Block* header);

  ZoneUnorderedSet<Block*> loop_body_{Asm().phase_zone()};
  LoopUnrollingAnalyzer analyzer_{Asm().phase_zone(),
                                  &Asm().modifiable_input_graph()};
  // {unrolling_} is true if a loop is currently being unrolled.
  UnrollingStatus unrolling_ = UnrollingStatus::kNotUnrolling;
  void* current_loop_header_ = nullptr;
  JSHeapBroker* broker_ = PipelineData::Get().broker();
};

template <class Next>
void LoopUnrollingReducer<Next>::UnrollLoop(Block* header) {
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
    Asm().CloneSubGraph(loop_body);
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
