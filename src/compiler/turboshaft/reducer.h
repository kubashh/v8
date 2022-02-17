// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_REDUCER_H_

#include "src/base/iterator.h"
#include "src/base/logging.h"
#include "src/base/small-vector.h"
#include "src/base/vector.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

struct AnalyzerBase {
  Zone* zone;
  const Graph& graph;

  void Run() {}
  bool BlockReachable(BlockIndex block) const { return true; }
  bool OpIsUsed(OpIndex i) const { return true; }
  OpIndex Replacement(OpIndex i) const { return i; }

  explicit AnalyzerBase(const Graph& graph, Zone* zone)
      : zone(zone), graph(graph) {}
};

struct LivenessAnalyzer : AnalyzerBase {
  using Base = AnalyzerBase;
  std::vector<uint8_t> op_used;

  LivenessAnalyzer(const Graph& graph, Zone* zone)
      : AnalyzerBase(graph, zone), op_used(graph.op_id_count()) {}

  bool OpIsUsed(OpIndex i) { return Base::OpIsUsed(i) && op_used[i.id()]; }

  void Run() { ComputeUsedOperations(); }

  void ComputeUsedOperations() {
    for (size_t unprocessed_count = graph.block_count();
         unprocessed_count > 0;) {
      BlockIndex block_index = static_cast<BlockIndex>(unprocessed_count - 1);
      --unprocessed_count;
      if (!BlockReachable(block_index)) continue;
      const Block& block = graph.Get(block_index);
      if (block.IsLoop()) {
        ProcessBlock<true>(block, &unprocessed_count);
      } else {
        ProcessBlock<false>(block, &unprocessed_count);
      }
    }
  }

  template <bool is_loop>
  void ProcessBlock(const Block& block, size_t* unprocessed_count) {
    for (const Operation& op : base::Reversed(graph.operations(block))) {
      if (op.properties().is_required_when_unused) {
        op_used[graph.Index(op).id()] = true;
      } else if (!OpIsUsed(graph.Index(op))) {
        continue;
      }
      if (is_loop && op.Is<PhiOp>()) {
        const PhiOp& phi = op.Cast<PhiOp>();
        if (!OpIsUsed(phi.inputs()[1])) {
          Block* backedge = block.predecessors[1];
          *unprocessed_count = std::max(
              *unprocessed_count,
              static_cast<size_t>(ToUnderlyingType(backedge->index)) + 1);
        }
      }
      for (OpIndex input : op.inputs()) {
        op_used[input.id()] = true;
      }
    }
  }
};

template <class Analyzer, class Assembler>
struct OptimizationPhase {
  Graph& input;
  Zone* zone;
  Assembler assembler;
  std::vector<Block*> block_mapping;
  std::vector<OpIndex> op_mapping;

  explicit OptimizationPhase(Graph* input, Zone* phase_zone)
      : input(*input),
        zone(input->graph_zone()),
        assembler(&input->GetOrCreateCompanion(), phase_zone),
        block_mapping(this->input.block_count(), nullptr),
        op_mapping(this->input.op_id_count(), OpIndex::Invalid()) {}

  void Run() {
    Analyzer analyzer(input, zone);
    analyzer.Run();

    for (const Block& block : input.blocks()) {
      if (!analyzer.BlockReachable(block.index)) continue;
      block_mapping[ToUnderlyingType(block.index)] =
          assembler.NewBlock(block.kind);
    }
    for (const Block& block : input.blocks()) {
      if (!analyzer.BlockReachable(block.index)) continue;
      if (!assembler.Bind(MapToNewGraph(block.index))) continue;
      assembler.current_block()->deferred = block.deferred;
      for (const Operation& op : input.operations(block)) {
        OpIndex index = input.Index(op);
        if (!analyzer.OpIsUsed(index)) continue;
        OpIndex replacement = analyzer.Replacement(index);
        if (replacement != index) {
          op_mapping[index.id()] = MapToNewGraph(replacement);
          continue;
        }
        OpIndex new_index;
        if (block.IsLoop() && op.Is<PhiOp>()) {
          const PhiOp& phi = op.Cast<PhiOp>();
          new_index = assembler.PendingLoopPhi(MapToNewGraph(phi.inputs()[0]),
                                               phi.rep, phi.inputs()[1]);
        } else {
          switch (op.opcode) {
#define EMIT_INSTR_CASE(Name)                \
  case Opcode::k##Name:                      \
    new_index = Reduce(op.Cast<Name##Op>()); \
    break;
            TURBOSHAFT_OPERATION_LIST(EMIT_INSTR_CASE)
#undef EMIT_INSTR_CASE
          }
        }
        op_mapping[index.id()] = new_index;
      }
    }
    // PrintF("graph size: %zu\n", assembler.graph().op_id_count());
    input.SwapWithCompanion();
  }

 private:
  OpIndex Reduce(const GotoOp& op) {
    Block* destination = MapToNewGraph(op.destination->index);
    if (op.destination->IsLoop()) {
      FixLoopPhis(destination, assembler.current_block());
    }
    return assembler.Goto(destination);
  }
  OpIndex Reduce(const BranchOp& op) {
    Block* if_true = MapToNewGraph(op.if_true->index);
    Block* if_false = MapToNewGraph(op.if_false->index);
    return assembler.Branch(MapToNewGraph(op.condition()), if_true, if_false);
  }
  OpIndex Reduce(const SwitchOp& op) {
    base::SmallVector<SwitchOp::Case, 16> cases;
    for (SwitchOp::Case c : op.cases) {
      cases.emplace_back(c.value, MapToNewGraph(c.destination->index));
    }
    return assembler.Switch(
        MapToNewGraph(op.input()),
        assembler.graph_zone()->CloneVector(base::VectorOf(cases)),
        MapToNewGraph(op.default_case->index));
  }
  OpIndex Reduce(const UnreachableOp& op) { return assembler.Unreachable(); }
  OpIndex Reduce(const BinaryOp& op) {
    return assembler.Binary(MapToNewGraph(op.left()), MapToNewGraph(op.right()),
                            op.kind, op.with_overflow_bit, op.rep);
  }
  OpIndex Reduce(const FloatUnaryOp& op) {
    return assembler.FloatUnary(MapToNewGraph(op.input()), op.kind, op.rep);
  }
  OpIndex Reduce(const ShiftOp& op) {
    return assembler.Shift(MapToNewGraph(op.left()), MapToNewGraph(op.right()),
                           op.kind, op.rep);
  }
  OpIndex Reduce(const EqualOp& op) {
    return assembler.Equal(MapToNewGraph(op.left()), MapToNewGraph(op.right()),
                           op.rep);
  }
  OpIndex Reduce(const ComparisonOp& op) {
    return assembler.Comparison(MapToNewGraph(op.left()),
                                MapToNewGraph(op.right()), op.kind, op.rep);
  }
  OpIndex Reduce(const ChangeOp& op) {
    return assembler.Change(MapToNewGraph(op.input()), op.kind, op.from, op.to);
  }
  OpIndex Reduce(const TaggedBitcastOp& op) {
    return assembler.TaggedBitcast(MapToNewGraph(op.input()), op.from, op.to);
  }
  OpIndex Reduce(const PendingVariableLoopPhiOp& op) { UNREACHABLE(); }
  OpIndex Reduce(const PendingLoopPhiOp& op) { UNREACHABLE(); }
  OpIndex Reduce(const ConstantOp& op) {
    return assembler.Constant(op.kind, op.storage);
  }
  OpIndex Reduce(const LoadOp& op) {
    return assembler.Load(MapToNewGraph(op.base()), op.kind, op.loaded_rep,
                          op.offset);
  }
  OpIndex Reduce(const IndexedLoadOp& op) {
    return assembler.IndexedLoad(MapToNewGraph(op.base()),
                                 MapToNewGraph(op.index()), op.kind,
                                 op.loaded_rep, op.offset, op.element_scale);
  }
  OpIndex Reduce(const StoreOp& op) {
    return assembler.Store(MapToNewGraph(op.base()), MapToNewGraph(op.value()),
                           op.kind, op.stored_rep, op.write_barrier, op.offset);
  }
  OpIndex Reduce(const IndexedStoreOp& op) {
    return assembler.IndexedStore(
        MapToNewGraph(op.base()), MapToNewGraph(op.index()),
        MapToNewGraph(op.value()), op.kind, op.stored_rep, op.write_barrier,
        op.offset, op.element_scale);
  }
  OpIndex Reduce(const ParameterOp& op) {
    return assembler.Parameter(op.parameter_index, op.debug_name);
  }
  OpIndex Reduce(const StackPointerGreaterThanOp& op) {
    return assembler.StackPointerGreaterThan(MapToNewGraph(op.stack_limit()),
                                             op.kind);
  }
  OpIndex Reduce(const LoadStackCheckOffsetOp& op) {
    return assembler.LoadStackCheckOffset();
  }
  OpIndex Reduce(const CheckLazyDeoptOp& op) {
    return assembler.CheckLazyDeopt(MapToNewGraph(op.call()),
                                    MapToNewGraph(op.frame_state()));
  }
  OpIndex Reduce(const DeoptimizeOp& op) {
    return assembler.Deoptimize(MapToNewGraph(op.frame_state()), op.parameters);
  }
  OpIndex Reduce(const DeoptimizeIfOp& op) {
    return assembler.DeoptimizeIf(MapToNewGraph(op.condition()),
                                  MapToNewGraph(op.frame_state()), op.negated,
                                  op.parameters);
  }
  OpIndex Reduce(const PhiOp& op) {
    base::SmallVector<OpIndex, 8> inputs = op.inputs();
    for (OpIndex& input : inputs) input = MapToNewGraph(input);
    return assembler.Phi(base::VectorOf(inputs), op.rep);
  }
  OpIndex Reduce(const FrameStateOp& op) {
    base::SmallVector<OpIndex, 16> inputs = op.inputs();
    for (OpIndex& input : inputs) input = MapToNewGraph(input);
    return assembler.FrameState(base::VectorOf(inputs), op.inlined, op.data);
  }
  OpIndex Reduce(const CallOp& op) {
    OpIndex callee = MapToNewGraph(op.callee());
    base::SmallVector<OpIndex, 16> arguments = op.arguments();
    for (OpIndex& arg : arguments) arg = MapToNewGraph(arg);
    return assembler.Call(callee, base::VectorOf(arguments), op.descriptor);
  }
  OpIndex Reduce(const ReturnOp& op) {
    base::SmallVector<OpIndex, 16> inputs = op.inputs();
    for (OpIndex& input : inputs) input = MapToNewGraph(input);
    return assembler.Return(base::VectorOf(inputs), op.pop_count);
  }
  OpIndex Reduce(const ProjectionOp& op) {
    return assembler.Projection(MapToNewGraph(op.input()), op.kind);
  }

  OpIndex MapToNewGraph(OpIndex old_index) {
    OpIndex result = op_mapping[old_index.id()];
    DCHECK(result.valid());
    return result;
  }
  Block* MapToNewGraph(BlockIndex old_index) {
    Block* result = block_mapping[ToUnderlyingType(old_index)];
    DCHECK_NOT_NULL(result);
    return result;
  }
  void FixLoopPhis(Block* loop, Block* backedge) {
    DCHECK(loop->IsLoop());
    for (Operation& op : assembler.graph().operations(*loop)) {
      if (auto* pending_phi = op.TryCast<PendingLoopPhiOp>()) {
        assembler.graph().template Replace<PhiOp>(
            assembler.graph().Index(*pending_phi),
            base::VectorOf({pending_phi->first(),
                            MapToNewGraph(pending_phi->old_backedge_index)}),
            pending_phi->rep);
      }
    }
  }
};

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_REDUCER_H_
