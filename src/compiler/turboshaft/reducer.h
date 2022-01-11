// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_REDUCER_H_

#include "src/base/iterator.h"
#include "src/base/logging.h"
#include "src/base/small-vector.h"
#include "src/compiler/turboshaft/cfg.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

struct AnalyzerBase {
  Zone* zone;
  const Graph& graph;

  void Run() {}
  bool BlockReachable(BlockIndex block) { return true; }
  bool OpIsUsed(OpIndex i) { return true; }
  OpIndex Replacement(OpIndex i) { return i; }

  explicit AnalyzerBase(const Graph& graph, Zone* zone)
      : zone(zone), graph(graph) {}
};

struct LivenessAnalyzer : AnalyzerBase {
  using Base = AnalyzerBase;
  ZoneVector<bool> block_reachable;
  ZoneVector<bool> op_used;

  LivenessAnalyzer(const Graph& graph, Zone* zone)
      : AnalyzerBase(graph, zone),
        block_reachable(graph.block_count(), zone),
        op_used(graph.op_count(), zone) {}

  bool BlockReachable(BlockIndex block) {
    return Base::BlockReachable(block) &&
           block_reachable[ToUnderlyingType(block)];
  }
  bool OpIsUsed(OpIndex i) {
    return Base::OpIsUsed(i) && op_used[ToUnderlyingType(i)];
  }

  void Run() {
    ComputeReachableBlocks();
    ComputeUsedOperations();
  }

  void ComputeReachableBlocks() {
    base::SmallVector<const Block*, 32> worklist;
    worklist.emplace_back(&graph.StartBlock());
    while (!worklist.empty()) {
      const Block* block = worklist.back();
      worklist.pop_back();
      if (!BlockReachable(block->index)) {
        block_reachable[ToUnderlyingType(block->index)] = true;
        for (const Block* succ : block->successors) {
          if (!BlockReachable(succ->index)) worklist.emplace_back(succ);
        }
      }
    }
  }
  void ComputeUsedOperations() {
    for (size_t unprocessed_count = graph.block_count(); unprocessed_count > 0;
         --unprocessed_count) {
      BlockIndex block_index = static_cast<BlockIndex>(unprocessed_count - 1);
      if (!BlockReachable(block_index)) continue;
      const Block& block = graph.Get(block_index);
      unprocessed_count = std::max(unprocessed_count,
                                   block.IsLoop() ? ProcessBlock<true>(block)
                                                  : ProcessBlock<false>(block));
    }
  }

  template <bool is_loop>
  size_t ProcessBlock(const Block& block) {
    size_t unprocessed_count = 0;
    for (const Operation& op : base::Reversed(graph.BlockIterator(block))) {
      if (op.properties().is_required_when_unused) {
        op_used[ToUnderlyingType(graph.Index(op))] = true;
      } else if (!OpIsUsed(graph.Index(op))) {
        continue;
      }
      if (is_loop && op.Is<LoopPhiOp>()) {
        const LoopPhiOp& phi = op.Cast<LoopPhiOp>();
        if (!OpIsUsed(phi.second())) {
          unprocessed_count = static_cast<size_t>(phi.backedge_block) + 1;
        }
      }
      for (OpIndex input : op.Inputs()) {
        op_used[ToUnderlyingType(input)] = true;
      }
    }
    return unprocessed_count;
  }
};

// template <class Analyzer, class Assembler>
struct OptimizationPhase {
  using Analyzer = LivenessAnalyzer;
  using Assembler = VarAssembler;
  Graph input;
  Zone* zone;
  Assembler assembler;
  ZoneVector<Block*> block_mapping;
  ZoneVector<OpIndex> op_mapping;

  explicit OptimizationPhase(Graph input)
      : input(std::move(input)),
        zone(input.graph_zone()),
        assembler(zone),
        block_mapping(input.block_count(), nullptr, zone),
        op_mapping(input.op_count(), OpIndex::kInvalid, zone) {}

  Graph Run() {
    Analyzer analyzer(input, zone);
    analyzer.Run();

    for (const Block* block : input.blocks()) {
      if (!analyzer.BlockReachable(block->index)) continue;
      block_mapping[ToUnderlyingType(block->index)] =
          assembler.NewBlock(block->kind);
    }
    for (const Block* block : input.blocks()) {
      if (!analyzer.BlockReachable(block->index)) continue;
      if (!assembler.Bind(MapToNewGraph(block->index))) continue;
      for (const Operation& op : input.BlockIterator(*block)) {
        OpIndex index = input.Index(op);
        if (!analyzer.OpIsUsed(index)) continue;
        OpIndex replacement = analyzer.Replacement(index);
        if (replacement != index) {
          op_mapping[ToUnderlyingType(index)] = MapToNewGraph(replacement);
          continue;
        }
        OpIndex new_index;
        if (op.Is<LoopPhiOp>()) {
          const LoopPhiOp& phi = op.Cast<LoopPhiOp>();
          new_index = assembler.Emit(
              PendingLoopPhiOp(MapToNewGraph(phi.first()), phi.second()));
        } else {
          base::Vector<const OpIndex> old_inputs = op.Inputs();
          base::SmallVector<OpIndex, 16> new_inputs(old_inputs.size());
          for (size_t i = 0; i < old_inputs.size(); ++i) {
            new_inputs[i] = MapToNewGraph(old_inputs[i]);
          }
          // TODO(tebbi): Check if a specialized ReplaceInputs in each switch
          // case is better for performance.
          OperationStorage new_op = op.ReplaceInputs(new_inputs.vector(), zone);
          switch (op.opcode) {
#define EMIT_INSTR_CASE(Name)                                                  \
  case Opcode::k##Name:                                                        \
    new_index = Emit(reinterpret_cast<Operation*>(&new_op)->Cast<Name##Op>()); \
    break;
            TURBOSHAFT_OPERATION_LIST(EMIT_INSTR_CASE)
#undef EMIT_INSTR_CASE
          }
        }
        op_mapping[ToUnderlyingType(index)] = new_index;
      }
    }
    return std::move(assembler.graph());
  }

 private:
  template <class Op>
  OpIndex Emit(const Op& op) {
    return assembler.Emit(op);
  }
  OpIndex Emit(const GotoOp& op) {
    Block* destination = MapToNewGraph(op.destination->index);
    if (op.destination->IsLoop()) {
      FixLoopPhis(destination, assembler.current_block());
    }
    assembler.Emit(GotoOp(destination));
    return OpIndex::kInvalid;
  }
  OpIndex Emit(const BranchOp& op) {
    Block* if_true = MapToNewGraph(op.if_true()->index);
    Block* if_false = MapToNewGraph(op.if_false()->index);
    assembler.Emit(BranchOp(op.condition(), if_true, if_false, zone));
    return OpIndex::kInvalid;
  }

  OpIndex MapToNewGraph(OpIndex old_index) {
    OpIndex result = op_mapping[ToUnderlyingType(old_index)];
    DCHECK_NE(result, OpIndex::kInvalid);
    return result;
  }
  Block* MapToNewGraph(BlockIndex old_index) {
    Block* result = block_mapping[ToUnderlyingType(old_index)];
    DCHECK_NOT_NULL(result);
    return result;
  }
  void FixLoopPhis(Block* loop, Block* backedge) {
    DCHECK(loop->IsLoop());
    for (Operation& op : assembler.graph().BlockIterator(*loop)) {
      if (!op.Is<PendingLoopPhiOp>()) continue;
      auto& pending_phi = op.Cast<PendingLoopPhiOp>();
      LoopPhiOp new_phi(pending_phi.first(),
                        MapToNewGraph(pending_phi.old_backedge_index),
                        backedge->index);
      assembler.graph().Replace(&pending_phi, new_phi);
    }
  }
};

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_REDUCER_H_
