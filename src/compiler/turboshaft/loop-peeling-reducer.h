// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LOOP_PEELING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_LOOP_PEELING_REDUCER_H_

#include "src/base/logging.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/loop-finder.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/optimization-phase.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class LoopUnrollingReducer;

template <class Next>
class LoopPeelingReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

  OpIndex REDUCE_INPUT_GRAPH(Goto)(OpIndex ig_idx, const GotoOp& gto) {
    // Note that the "ShouldSkipOptimizationStep" is placed in the part of
    // this Reduce method triggering the peeling rather than at the begining.
    // This is because the backedge skipping is not an optimization but a
    // mandatory lowering when peeling is being performed.
    LABEL_BLOCK(no_change) { return Next::ReduceInputGraphGoto(ig_idx, gto); }
    if (!v8_flags.turboshaft_loop_peeling) goto no_change;

    Block* dst = gto.destination;
    if (dst->IsLoop() && !gto.is_backedge &&
        !loop_finder_.GetLoopInfo(dst).has_inner_loops) {
      if (ShouldSkipOptimizationStep()) goto no_change;
      PeelFirstIteration(dst);
      if (__ generating_unreachable_operations()) return OpIndex::Invalid();
      // After emitting the peeled iteration, we emit the Goto to the loop
      // header (which is not a backedge anymore).
      // Note that most Operations cannot be build without allocating some
      // storage first because their inputs are stored after them in memory
      // despite not counting toward their size as far as sizeof is concerned.
      // However, GotoOp has no inputs, so we can easily create a new one.
      static_assert(GotoOp::input_count == 0);
      GotoOp new_gto = GotoOp(dst, false);
      return Next::ReduceInputGraphGoto(ig_idx, new_gto);
    } else if (is_peeling_ && dst == current_loop_header_) {
      // We skip the backedge of the loop: PeelFirstIeration will instead emit a
      // forward edge to the non-peeled header.
      return OpIndex::Invalid();
    }

    goto no_change;
  }

  OpIndex REDUCE_INPUT_GRAPH(Call)(OpIndex ig_idx, const CallOp& call) {
    LABEL_BLOCK(no_change) { return Next::ReduceInputGraphCall(ig_idx, call); }
    if (ShouldSkipOptimizationStep()) goto no_change;
    if (!v8_flags.turboshaft_loop_peeling) goto no_change;

    if (is_peeling_ && call.IsStackCheck(__ input_graph(), broker_,
                                         StackCheckKind::kJSIterationBody)) {
      // We remove the stack check of the peeled iteration.
      return OpIndex::Invalid();
    }

    goto no_change;
  }

  OpIndex REDUCE_INPUT_GRAPH(StackCheck)(OpIndex ig_idx,
                                         const StackCheckOp& stack_check) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphStackCheck(ig_idx, stack_check);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;
    if (!v8_flags.turboshaft_loop_peeling) goto no_change;

    if (is_peeling_) {
      // We remove the stack check of the peeled iteration.
      return OpIndex::Invalid();
    }

    goto no_change;
  }

 private:
  void PeelFirstIteration(Block* header);

  bool is_peeling_ = false;
  Block* current_loop_header_ = nullptr;

  ZoneUnorderedSet<Block*> loop_body_{__ phase_zone()};
  LoopFinder loop_finder_{__ phase_zone(), &__ modifiable_input_graph()};
  JSHeapBroker* broker_ = PipelineData::Get().broker();
};

template <class Next>
void LoopPeelingReducer<Next>::PeelFirstIteration(Block* header) {
  DCHECK(!is_peeling_);

  current_loop_header_ = header;
  is_peeling_ = true;

  // Emitting the peeled iteration
  auto loop_body = loop_finder_.GetLoopBody(header);
  __ CloneSubGraph(loop_body, /* keep_loop_kinds */ false);

  is_peeling_ = false;
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LOOP_PEELING_REDUCER_H_
