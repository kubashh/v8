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
#include "src/compiler/operator.h"
#include "src/compiler/schedule.h"
#include "src/compiler/turboshaft/cfg.h"
#include "src/compiler/turboshaft/instructions.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

namespace {

struct GraphBuilder {
  Zone* graph_zone;
  Zone* temp_zone;
  Schedule& schedule;
  BasicAssembler assembler{graph_zone};
  NodeAuxData<InstrIndex> instr_mapping{temp_zone};
  ZoneVector<Block*> block_mapping{schedule.RpoBlockCount(), temp_zone};

  Graph Run();

 private:
  InstrIndex Map(Node* old_node) {
    InstrIndex result = instr_mapping.Get(old_node);
    DCHECK_NE(result, InstrIndex::kInvalid);
    return result;
  }
  Block* Map(BasicBlock* block) {
    Block* result = block_mapping[block->rpo_number()];
    DCHECK_NOT_NULL(result);
    return result;
  }

  template <class Instr>
  InstrIndex Emit(const Instr& instr) {
    return assembler.Emit(instr);
  }

  void FixLoopPhis(Block* loop, Block* backedge) {
    DCHECK(loop->IsLoop());
    for (Instruction& instr : assembler.graph().BlockIterator(*loop)) {
      if (!instr.Is<PendingLoopPhiInstr>()) continue;
      auto& pending_phi = instr.Cast<PendingLoopPhiInstr>();
      LoopPhiInstr new_phi(pending_phi.first(),
                           Map(pending_phi.old_backedge_node), backedge->index);
      assembler.graph().Replace(&pending_phi, new_phi);
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
  InstrIndex Process(Node* node, BasicBlock* block);
};

Graph GraphBuilder::Run() {
  for (BasicBlock* block : *schedule.rpo_order()) {
    block_mapping[block->rpo_number()] = assembler.NewBlock(BlockKind(block));
  }
  for (BasicBlock* block : *schedule.rpo_order()) {
    if (!assembler.Bind(Map(block))) continue;
    for (Node* node : *block->nodes()) {
      InstrIndex i = Process(node, block);
      // PrintF("#%i -> $%u\n", node->id(), ToUnderlyingType(i));
      instr_mapping.Set(node, i);
    }
    if (Node* node = block->control_input()) {
      InstrIndex i = Process(node, block);
      // PrintF("#%i -> $%u\n", node->id(), ToUnderlyingType(i));
      instr_mapping.Set(node, i);
    }
    switch (block->control()) {
      case BasicBlock::kGoto:
        DCHECK_EQ(block->SuccessorCount(), 1);
        Emit(GotoInstr(Map(block->SuccessorAt(0))));
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
  return std::move(assembler.graph());
}

InstrIndex GraphBuilder::Process(Node* node, BasicBlock* block) {
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
      return InstrIndex::kInvalid;

    case IrOpcode::kParameter: {
      const ParameterInfo& info = ParameterInfoOf(op);
      return Emit(ParameterInstr(info.index(), info.debug_name()));
    }

    case IrOpcode::kInt64Constant:
      return Emit(ConstantInstr::Word64(OpParameter<int64_t>(op)));
    case IrOpcode::kInt32Constant:
      return Emit(ConstantInstr::Word64(OpParameter<int32_t>(op)));
    case IrOpcode::kHeapConstant:
      return Emit(ConstantInstr::HeapObject(HeapConstantOf(op)));
    case IrOpcode::kCompressedHeapConstant:
      return Emit(ConstantInstr::CompressedHeapObject(HeapConstantOf(op)));
    case IrOpcode::kExternalConstant:
      return Emit(ConstantInstr::External(OpParameter<ExternalReference>(op)));

    case IrOpcode::kWord32And:
      return Emit(BitwiseAndInstr(Map(node->InputAt(0)), Map(node->InputAt(1)),
                                  MachineRepresentation::kWord32));
    case IrOpcode::kWord64And:
      return Emit(BitwiseAndInstr(Map(node->InputAt(0)), Map(node->InputAt(1)),
                                  MachineRepresentation::kWord64));

    case IrOpcode::kWord32Equal:
      return Emit(EqualInstr(Map(node->InputAt(0)), Map(node->InputAt(1)),
                             MachineRepresentation::kWord32));
    case IrOpcode::kWord64Equal:
      return Emit(EqualInstr(Map(node->InputAt(0)), Map(node->InputAt(1)),
                             MachineRepresentation::kWord64));

    case IrOpcode::kInt32Add:
      return Emit(AddInstr(Map(node->InputAt(0)), Map(node->InputAt(1)),
                           MachineRepresentation::kWord32));
    case IrOpcode::kInt64Add:
      return Emit(AddInstr(Map(node->InputAt(0)), Map(node->InputAt(1)),
                           MachineRepresentation::kWord64));

    case IrOpcode::kInt32Sub:
      return Emit(SubInstr(Map(node->InputAt(0)), Map(node->InputAt(1)),
                           MachineRepresentation::kWord32));
    case IrOpcode::kInt64Sub:
      return Emit(SubInstr(Map(node->InputAt(0)), Map(node->InputAt(1)),
                           MachineRepresentation::kWord64));

    case IrOpcode::kLoad:
      return Emit(
          LoadInstr::Raw(LoadRepresentationOf(op), Map(node->InputAt(0))));

    case IrOpcode::kStackPointerGreaterThan:
      return Emit(StackPointerGreaterThanInstr(StackCheckKindOf(op),
                                               Map(node->InputAt(0))));
    case IrOpcode::kLoadStackCheckOffset:
      return Emit(LoadStackCheckOffsetInstr());

    case IrOpcode::kBranch:
      DCHECK_EQ(block->SuccessorCount(), 2);
      return Emit(BranchInstr(Map(node->InputAt(0)), Map(block->SuccessorAt(0)),
                              Map(block->SuccessorAt(1)), graph_zone));

    case IrOpcode::kCall: {
      auto call_descriptor = CallDescriptorOf(op);
      base::SmallVector<InstrIndex, 16> inputs;
      for (int i = 1; i < static_cast<int>(call_descriptor->InputCount());
           ++i) {
        inputs.emplace_back(Map(node->InputAt(i)));
      }
      InstrIndex call = Emit(CallInstr(call_descriptor, Map(node->InputAt(0)),
                                       inputs.vector(), graph_zone));
      if (!call_descriptor->NeedsFrameState()) return call;
      FrameState frame_state{
          node->InputAt(static_cast<int>(call_descriptor->InputCount()))};
      InstrIndex checkpoint = Emit(
          CheckpointInstr::Full(base::VectorOf<InstrIndex>({}),
                                frame_state.frame_state_info(), graph_zone));
      Emit(CheckLazyDeoptInstr(checkpoint));
      return call;
    }

    case IrOpcode::kReturn: {
      Node* pop_count = node->InputAt(0);
      if (pop_count->opcode() != IrOpcode::kInt32Constant ||
          OpParameter<int32_t>(pop_count->op()) != 0) {
        UNIMPLEMENTED();
      }
      return Emit(ReturnInstr(Map(node->InputAt(1))));
    }

    default:
      PrintF("unsupportd node type:\n");
      node->Print();
      UNIMPLEMENTED();
  }
}

}  // namespace

Graph BuildGraph(Schedule* schedule, Zone* graph_zone, Zone* temp_zone) {
  return GraphBuilder{graph_zone, temp_zone, *schedule}.Run();
}

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8
