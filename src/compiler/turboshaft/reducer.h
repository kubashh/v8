// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_REDUCER_H_

#include "src/base/logging.h"
#include "src/base/small-vector.h"
#include "src/compiler/turboshaft/cfg.h"
#include "src/compiler/turboshaft/instructions.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

struct AnalyzerBase {
  Zone* zone;
  const Graph& graph;

  void Run() {}
  bool BlockReachable(BlockIndex block) { return true; }
  bool InstrIsUsed(InstrIndex i) { return true; }
  InstrIndex Replacement(InstrIndex i) { return i; }

  explicit AnalyzerBase(const Graph& graph, Zone* zone)
      : zone(zone), graph(graph) {}
};

struct LivenessAnalyzer : AnalyzerBase {
  using Base = AnalyzerBase;
  ZoneVector<bool> block_reachable;
  ZoneVector<bool> instr_used;

  LivenessAnalyzer(const Graph& graph, Zone* zone)
      : AnalyzerBase(graph, zone),
        block_reachable(graph.block_count(), zone),
        instr_used(graph.instr_count(), zone) {}

  bool BlockReachable(BlockIndex block) {
    return Base::BlockReachable(block) &&
           block_reachable[ToUnderlyingType(block)];
  }
  bool InstrIsUsed(InstrIndex i) {
    return Base::InstrIsUsed(i) && instr_used[ToUnderlyingType(i)];
  }

  void Run() {
    ComputeReachableBlocks();
    ComputeUsedInstructions();
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
  void ComputeUsedInstructions() {
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
    for (const Instruction& instr : graph.BlockIterator(block)) {
      if (instr.IsRequiredWhenUnused()) {
        instr_used[ToUnderlyingType(graph.Index(instr))] = true;
      } else if (!InstrIsUsed(graph.Index(instr))) {
        continue;
      }
      if (is_loop && instr.Is<LoopPhiInstr>()) {
        const LoopPhiInstr& phi = instr.Cast<LoopPhiInstr>();
        if (!InstrIsUsed(phi.second())) {
          unprocessed_count = static_cast<size_t>(phi.backedge_block) + 1;
        }
      }
      for (InstrIndex input : instr.Inputs()) {
        instr_used[ToUnderlyingType(input)] = true;
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
  ZoneVector<InstrIndex> instr_mapping;

  explicit OptimizationPhase(Graph input)
      : input(std::move(input)),
        zone(input.graph_zone()),
        assembler(zone),
        block_mapping(input.block_count(), nullptr, zone),
        instr_mapping(input.instr_count(), InstrIndex::kInvalid, zone) {}

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
      assembler.Bind(MapToNewGraph(block->index));
      for (const Instruction& instr : input.BlockIterator(*block)) {
        InstrIndex index = input.Index(instr);
        if (!analyzer.InstrIsUsed(index)) continue;
        InstrIndex replacement = analyzer.Replacement(index);
        if (replacement != index) {
          instr_mapping[ToUnderlyingType(index)] = MapToNewGraph(replacement);
          continue;
        }
        InstrIndex new_index;
        if (instr.Is<LoopPhiInstr>()) {
          const LoopPhiInstr& phi = instr.Cast<LoopPhiInstr>();
          new_index = assembler.Emit(
              PendingLoopPhiInstr(MapToNewGraph(phi.first()), phi.second()));
        } else {
          base::Vector<const InstrIndex> old_inputs = instr.Inputs();
          base::SmallVector<InstrIndex, 16> new_inputs(old_inputs.size());
          for (size_t i = 0; i < old_inputs.size(); ++i) {
            new_inputs[i] = MapToNewGraph(old_inputs[i]);
          }
          // TODO(tebbi): Check if a specialized ReplaceInputs in each switch
          // case is better for performance.
          InstructionStorage new_instr =
              instr.ReplaceInputs(new_inputs.vector(), zone);
          switch (instr.opcode) {
#define EMIT_INSTR_CASE(Name)                                                  \
  case Opcode::k##Name:                                                        \
    new_index =                                                                \
        Emit(reinterpret_cast<Instruction*>(&new_instr)->Cast<Name##Instr>()); \
    break;
            INSTRUCTION_LIST(EMIT_INSTR_CASE)
#undef EMIT_INSTR_CASE
            case Opcode::kNumberOfOpcodes:
              UNREACHABLE();
          }
        }
        instr_mapping[ToUnderlyingType(index)] = new_index;
      }
    }
    return std::move(assembler.graph());
  }

 private:
  template <class Instr>
  InstrIndex Emit(const Instr& instr) {
    return assembler.Emit(instr);
  }
  InstrIndex Emit(const GotoInstr& instr) {
    Block* destination = MapToNewGraph(instr.destination->index);
    if (instr.destination->IsLoop()) {
      FixLoopPhis(destination, assembler.current_block());
    }
    assembler.Emit(GotoInstr(destination));
    return InstrIndex::kInvalid;
  }
  InstrIndex Emit(const BranchInstr& instr) {
    Block* if_true = MapToNewGraph(instr.if_true()->index);
    Block* if_false = MapToNewGraph(instr.if_false()->index);
    assembler.Emit(BranchInstr(instr.condition(), if_true, if_false, zone));
    return InstrIndex::kInvalid;
  }

  InstrIndex MapToNewGraph(InstrIndex old_index) {
    InstrIndex result = instr_mapping[ToUnderlyingType(old_index)];
    DCHECK_NE(result, InstrIndex::kInvalid);
    return result;
  }
  Block* MapToNewGraph(BlockIndex old_index) {
    Block* result = block_mapping[ToUnderlyingType(old_index)];
    DCHECK_NOT_NULL(result);
    return result;
  }
  void FixLoopPhis(Block* loop, Block* backedge) {
    DCHECK(loop->IsLoop());
    for (Instruction& instr : assembler.graph().BlockIterator(*loop)) {
      if (!instr.Is<PendingLoopPhiInstr>()) continue;
      auto& pending_phi = instr.Cast<PendingLoopPhiInstr>();
      LoopPhiInstr new_phi(pending_phi.first(),
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
