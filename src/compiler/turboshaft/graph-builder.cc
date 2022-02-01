// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/graph-builder.h"

#include "src/base/logging.h"
#include "src/base/small-vector.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-aux-data.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/operator.h"
#include "src/compiler/schedule.h"
#include "src/compiler/turboshaft/cfg.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

namespace {

struct GraphBuilder {
  Zone* graph_zone;
  Zone* temp_zone;
  Schedule& schedule;
  BasicAssembler assembler;
  NodeAuxData<OpIndex> op_mapping{temp_zone};
  ZoneVector<Block*> block_mapping{schedule.RpoBlockCount(), temp_zone};

  void Run();

 private:
  OpIndex Map(Node* old_node) {
    OpIndex result = op_mapping.Get(old_node);
    DCHECK(result.valid());
    return result;
  }
  Block* Map(BasicBlock* block) {
    Block* result = block_mapping[block->rpo_number()];
    DCHECK_NOT_NULL(result);
    return result;
  }

  void FixLoopPhis(Block* loop, Block* backedge) {
    DCHECK(loop->IsLoop());
    for (Operation& op : assembler.graph().BlockIterator(*loop)) {
      if (!op.Is<PendingLoopPhiOp>()) continue;
      auto& pending_phi = op.Cast<PendingLoopPhiOp>();
      assembler.graph().Replace<PhiOp>(
          assembler.graph().Index(pending_phi),
          base::VectorOf(
              {pending_phi.first(), Map(pending_phi.old_backedge_node)}));
    }
  }

  Block::Kind BlockKind(BasicBlock* block) {
    switch (block->front()->opcode()) {
      case IrOpcode::kStart:
      case IrOpcode::kEnd:
      case IrOpcode::kMerge:
        return Block::Kind::kMerge;
      case IrOpcode::kIfTrue:
      case IrOpcode::kIfFalse:
      case IrOpcode::kIfValue:
      case IrOpcode::kIfDefault:
      case IrOpcode::kIfSuccess:
      case IrOpcode::kIfException:
        return Block::Kind::kBranch;
      case IrOpcode::kLoop:
        return Block::Kind::kLoop;
      default:
        block->front()->Print();
        UNREACHABLE();
    }
  }
  OpIndex Process(Node* node, BasicBlock* block);
};

void GraphBuilder::Run() {
  for (BasicBlock* block : *schedule.rpo_order()) {
    block_mapping[block->rpo_number()] = assembler.NewBlock(BlockKind(block));
  }
  for (BasicBlock* block : *schedule.rpo_order()) {
    if (!assembler.Bind(Map(block))) continue;
    for (Node* node : *block->nodes()) {
      OpIndex i = Process(node, block);
      // PrintF("#%i -> $%u\n", node->id(), ToUnderlyingType(i));
      op_mapping.Set(node, i);
    }
    if (Node* node = block->control_input()) {
      OpIndex i = Process(node, block);
      // PrintF("#%i -> $%u\n", node->id(), ToUnderlyingType(i));
      op_mapping.Set(node, i);
    }
    switch (block->control()) {
      case BasicBlock::kGoto:
        DCHECK_EQ(block->SuccessorCount(), 1);
        assembler.Goto(Map(block->SuccessorAt(0)));
        break;
      case BasicBlock::kBranch:
      case BasicBlock::kReturn:
        break;
      case BasicBlock::kCall:
      case BasicBlock::kSwitch:
      case BasicBlock::kDeoptimize:
      case BasicBlock::kTailCall:
      case BasicBlock::kThrow:
        UNIMPLEMENTED();
      case BasicBlock::kNone:
        UNREACHABLE();
    }
    DCHECK_NULL(assembler.current_block());
  }
}

OpIndex GraphBuilder::Process(Node* node, BasicBlock* block) {
  const Operator* op = node->op();
  switch (op->opcode()) {
    case IrOpcode::kStart:
    case IrOpcode::kMerge:
    case IrOpcode::kLoop:
    case IrOpcode::kIfTrue:
    case IrOpcode::kIfFalse:
    case IrOpcode::kTypedStateValues:
    case IrOpcode::kFrameState:
    case IrOpcode::kEffectPhi:
      return OpIndex::Invalid();

    case IrOpcode::kParameter: {
      const ParameterInfo& info = ParameterInfoOf(op);
      return assembler.Parameter(info.index(), info.debug_name());
    }

    case IrOpcode::kInt64Constant:
      return assembler.Constant(ConstantOp::Kind::kWord64,
                                OpParameter<int64_t>(op));
    case IrOpcode::kInt32Constant:
      return assembler.Constant(ConstantOp::Kind::kWord32,
                                OpParameter<int32_t>(op));
    case IrOpcode::kHeapConstant:
      return assembler.Constant(ConstantOp::Kind::kHeapObject,
                                HeapConstantOf(op));
    case IrOpcode::kCompressedHeapConstant:
      return assembler.Constant(ConstantOp::Kind::kCompressedHeapObject,
                                HeapConstantOf(op));
    case IrOpcode::kExternalConstant:
      return assembler.Constant(ConstantOp::Kind::kExternal,
                                OpParameter<ExternalReference>(op));

    case IrOpcode::kWord32And:
      return assembler.BitwiseAnd(Map(node->InputAt(0)), Map(node->InputAt(1)),
                                  MachineRepresentation::kWord32);
    case IrOpcode::kWord64And:
      return assembler.BitwiseAnd(Map(node->InputAt(0)), Map(node->InputAt(1)),
                                  MachineRepresentation::kWord64);

    case IrOpcode::kWord32Equal:
      return assembler.Equal(Map(node->InputAt(0)), Map(node->InputAt(1)),
                             MachineRepresentation::kWord32);
    case IrOpcode::kWord64Equal:
      return assembler.Equal(Map(node->InputAt(0)), Map(node->InputAt(1)),
                             MachineRepresentation::kWord64);

    case IrOpcode::kInt32Add:
      return assembler.Add(Map(node->InputAt(0)), Map(node->InputAt(1)),
                           MachineRepresentation::kWord32);
    case IrOpcode::kInt64Add:
      return assembler.Add(Map(node->InputAt(0)), Map(node->InputAt(1)),
                           MachineRepresentation::kWord64);

    case IrOpcode::kInt32Sub:
      return assembler.Sub(Map(node->InputAt(0)), Map(node->InputAt(1)),
                           MachineRepresentation::kWord32);
    case IrOpcode::kInt64Sub:
      return assembler.Sub(Map(node->InputAt(0)), Map(node->InputAt(1)),
                           MachineRepresentation::kWord64);

    case IrOpcode::kLoad:
      return assembler.Load(LoadOp::Kind::kRaw, LoadRepresentationOf(op),
                            Map(node->InputAt(0)));

    case IrOpcode::kStackPointerGreaterThan:
      return assembler.StackPointerGreaterThan(StackCheckKindOf(op),
                                               Map(node->InputAt(0)));
    case IrOpcode::kLoadStackCheckOffset:
      return assembler.LoadStackCheckOffset();

    case IrOpcode::kBranch:
      DCHECK_EQ(block->SuccessorCount(), 2);
      return assembler.Branch(Map(node->InputAt(0)), Map(block->SuccessorAt(0)),
                              Map(block->SuccessorAt(1)));

    case IrOpcode::kCall: {
      auto call_descriptor = CallDescriptorOf(op);
      base::SmallVector<OpIndex, 16> inputs;
      for (int i = 1; i < static_cast<int>(call_descriptor->InputCount());
           ++i) {
        inputs.emplace_back(Map(node->InputAt(i)));
      }
      OpIndex call = assembler.Call(call_descriptor, Map(node->InputAt(0)),
                                    base::VectorOf(inputs));
      if (!call_descriptor->NeedsFrameState()) return call;
      FrameState frame_state{
          node->InputAt(static_cast<int>(call_descriptor->InputCount()))};
      OpIndex checkpoint = assembler.Checkpoint(base::VectorOf<OpIndex>({}));
      assembler.CheckLazyDeopt(checkpoint);
      return call;
    }

    case IrOpcode::kReturn: {
      Node* pop_count = node->InputAt(0);
      if (pop_count->opcode() != IrOpcode::kInt32Constant) {
        UNIMPLEMENTED();
      }
      base::SmallVector<OpIndex, 4> return_values;
      for (int i = 1; i < node->op()->ValueInputCount(); ++i) {
        return_values.push_back(Map(node->InputAt(i)));
      }
      return assembler.Return(base::VectorOf(return_values),
                              OpParameter<int32_t>(pop_count->op()));
    }

    default:
      PrintF("unsupportd node type:\n");
      node->Print();
      UNIMPLEMENTED();
  }
}

}  // namespace

void BuildGraph(Schedule* schedule, Zone* graph_zone, Zone* temp_zone,
                Graph* graph) {
  GraphBuilder{graph_zone, temp_zone, *schedule, BasicAssembler(graph)}.Run();
}

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8
