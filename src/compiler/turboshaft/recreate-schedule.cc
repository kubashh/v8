// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/recreate-schedule.h"

#include <initializer_list>

#include "src/base/logging.h"
#include "src/base/safe_conversions.h"
#include "src/base/small-vector.h"
#include "src/base/template-utils.h"
#include "src/base/vector.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/schedule.h"
#include "src/compiler/scheduler.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/turboshaft/cfg.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

namespace {

struct ScheduleBuilder {
  const Graph& input;
  CallDescriptor* call_descriptor;
  Zone* zone;

  Schedule* const schedule =
      zone->New<Schedule>(zone, 1.1 * input.op_id_count());
  compiler::Graph* const tf_graph = zone->New<compiler::Graph>(zone);
  compiler::MachineOperatorBuilder machine{
      zone, MachineType::PointerRepresentation(),
      InstructionSelector::SupportedMachineOperatorFlags(),
      InstructionSelector::AlignmentRequirements()};
  compiler::CommonOperatorBuilder common{zone};
  compiler::SimplifiedOperatorBuilder simplified{zone};
  compiler::BasicBlock* current_block = schedule->start();
  const Block* current_input_block = nullptr;
  base::SmallVector<Node*, 16> parameters{};
  std::vector<BasicBlock*> blocks = {};
  std::vector<Node*> nodes{input.op_id_count()};
  std::vector<std::pair<Node*, OpIndex>> loop_phis = {};

  RecreateScheduleResult Run();
  Node* MakeNode(const Operator* op, base::Vector<Node* const> inputs);
  Node* MakeNode(const Operator* op, std::initializer_list<Node*> inputs) {
    return MakeNode(op, base::VectorOf(inputs));
  }
  Node* AddNode(const Operator* op, base::Vector<Node* const> inputs);
  Node* AddNode(const Operator* op, std::initializer_list<Node*> inputs) {
    return AddNode(op, base::VectorOf(inputs));
  }
  Node* GetNode(OpIndex i) { return nodes[i.id()]; }
  BasicBlock* GetBlock(const Block& block) {
    return blocks[ToUnderlyingType(block.index)];
  }
  Node* IntPtrConstant(intptr_t value) {
    return AddNode(machine.Is64() ? common.Int64Constant(value)
                                  : common.Int32Constant(
                                        base::checked_cast<int32_t>(value)),
                   {});
  }
  Node* IntPtrAdd(Node* a, Node* b) {
    return AddNode(machine.Is64() ? machine.Int64Add() : machine.Int32Add(),
                   {a, b});
  }
  void ProcessOperation(const Operation& op);
#define DECL_PROCESS_OPERATION(Name) Node* ProcessOperation(const Name##Op& op);
  TURBOSHAFT_OPERATION_LIST(DECL_PROCESS_OPERATION)
#undef DECL_PROCESS_OPERATION
};

Node* ScheduleBuilder::MakeNode(const Operator* op,
                                base::Vector<Node* const> inputs) {
  DCHECK_NOT_NULL(current_block);
  Node* node = tf_graph->NewNodeUnchecked(op, static_cast<int>(inputs.size()),
                                          inputs.data());
  return node;
}
Node* ScheduleBuilder::AddNode(const Operator* op,
                               base::Vector<Node* const> inputs) {
  Node* node = MakeNode(op, inputs);
  schedule->AddNode(current_block, node);
  return node;
}

RecreateScheduleResult ScheduleBuilder::Run() {
  DCHECK_GE(input.block_count(), 2);
  blocks.reserve(input.block_count());
  blocks.push_back(current_block);
  for (size_t i = 1; i < input.block_count() - 1; ++i) {
    blocks.push_back(schedule->NewBasicBlock());
  }
  blocks.push_back(schedule->end());

  size_t param_count = call_descriptor->ParameterCount();
  tf_graph->SetStart(
      tf_graph->NewNode(common.Start(static_cast<int>(param_count) + 1)));
  // if (call_descriptor->IsJSFunctionCall()) {
  //   target_parameter_ = AddNode(
  //       common()->Parameter(Linkage::kJSCallClosureParamIndex),
  //       graph->start());
  // }
  for (size_t i = 0; i < param_count; ++i) {
    parameters.push_back(
        AddNode(common.Parameter(static_cast<int>(i)), {tf_graph->start()}));
  }
  tf_graph->SetEnd(tf_graph->NewNode(common.End(0)));

  for (const Block& block : input.blocks()) {
    current_input_block = &block;
    current_block = GetBlock(block);
    for (const Operation& op : input.operations(block)) {
      DCHECK_NOT_NULL(current_block);
      ProcessOperation(op);
    }
  }

  for (auto& p : loop_phis) {
    p.first->ReplaceInput(1, GetNode(p.second));
  }

  DCHECK(schedule->rpo_order()->empty());
  Scheduler::ComputeSpecialRPO(zone, schedule);
  Scheduler::GenerateDominatorTree(schedule);

  return {tf_graph, schedule};
}

void ScheduleBuilder::ProcessOperation(const Operation& op) {
  Node* node;
  switch (op.opcode) {
#define SWITCH_CASE(Name)                         \
  case Opcode::k##Name:                           \
    node = ProcessOperation(op.Cast<Name##Op>()); \
    break;
    TURBOSHAFT_OPERATION_LIST(SWITCH_CASE)
#undef SWITCH_CASE
  }
  nodes[input.Index(op).id()] = node;
}

Node* ScheduleBuilder::ProcessOperation(const AddOp& op) {
  const Operator* o;
  switch (op.rep) {
    case MachineRepresentation::kWord32:
      o = machine.Int32Add();
      break;
    case MachineRepresentation::kWord64:
      o = machine.Int64Add();
      break;
    case MachineRepresentation::kFloat32:
      o = machine.Float32Add();
      break;
    case MachineRepresentation::kFloat64:
      o = machine.Float64Add();
      break;
    default:
      UNREACHABLE();
  }
  return AddNode(o, {GetNode(op.left()), GetNode(op.right())});
}
Node* ScheduleBuilder::ProcessOperation(const SubOp& op) {
  const Operator* o;
  switch (op.rep) {
    case MachineRepresentation::kWord32:
      o = machine.Int32Sub();
      break;
    case MachineRepresentation::kWord64:
      o = machine.Int64Sub();
      break;
    case MachineRepresentation::kFloat32:
      o = machine.Float32Sub();
      break;
    case MachineRepresentation::kFloat64:
      o = machine.Float64Sub();
      break;
    default:
      UNREACHABLE();
  }
  return AddNode(o, {GetNode(op.left()), GetNode(op.right())});
}
Node* ScheduleBuilder::ProcessOperation(const BitwiseAndOp& op) {
  const Operator* o;
  switch (op.rep) {
    case MachineRepresentation::kWord32:
      o = machine.Word32And();
      break;
    case MachineRepresentation::kWord64:
      o = machine.Word64And();
      break;
    default:
      UNREACHABLE();
  }
  return AddNode(o, {GetNode(op.left()), GetNode(op.right())});
}
Node* ScheduleBuilder::ProcessOperation(const EqualOp& op) {
  const Operator* o;
  switch (op.rep) {
    case MachineRepresentation::kWord32:
      o = machine.Word32Equal();
      break;
    case MachineRepresentation::kWord64:
      o = machine.Word64Equal();
      break;
    case MachineRepresentation::kFloat32:
      o = machine.Float32Equal();
      break;
    case MachineRepresentation::kFloat64:
      o = machine.Float64Equal();
      break;
    default:
      UNREACHABLE();
  }
  return AddNode(o, {GetNode(op.left()), GetNode(op.right())});
}
Node* ScheduleBuilder::ProcessOperation(const PendingVariableLoopPhiOp& op) {
  UNREACHABLE();
}
Node* ScheduleBuilder::ProcessOperation(const PendingLoopPhiOp& op) {
  UNREACHABLE();
}
Node* ScheduleBuilder::ProcessOperation(const ConstantOp& op) {
  switch (op.kind) {
    case ConstantOp::Kind::kWord32:
      return AddNode(common.Int32Constant(op.word32()), {});
    case ConstantOp::Kind::kWord64:
      return AddNode(common.Int64Constant(op.word64()), {});
    case ConstantOp::Kind::kExternal:
      return AddNode(common.ExternalConstant(op.external_reference()), {});
    case ConstantOp::Kind::kHeapObject:
      return AddNode(common.HeapConstant(op.handle()), {});
    case ConstantOp::Kind::kCompressedHeapObject:
      return AddNode(common.CompressedHeapConstant(op.handle()), {});
  }
}
Node* ScheduleBuilder::ProcessOperation(const LoadOp& op) {
  intptr_t offset = op.offset;
  if (op.kind == LoadOp::Kind::kOnHeap) {
    offset -= kHeapObjectTag;
  }
  Node* base = GetNode(op.base());
  return AddNode(machine.Load(op.loaded_rep), {base, IntPtrConstant(offset)});
}
Node* ScheduleBuilder::ProcessOperation(const ParameterOp& op) {
  return parameters[op.parameter_index];
}
Node* ScheduleBuilder::ProcessOperation(const GotoOp& op) {
  schedule->AddGoto(current_block,
                    blocks[ToUnderlyingType(op.destination->index)]);
  current_block = nullptr;
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const StackPointerGreaterThanOp& op) {
  return AddNode(machine.StackPointerGreaterThan(op.kind),
                 {GetNode(op.stack_limit())});
}
Node* ScheduleBuilder::ProcessOperation(const LoadStackCheckOffsetOp& op) {
  return AddNode(machine.LoadStackCheckOffset(), {});
}
Node* ScheduleBuilder::ProcessOperation(const CheckLazyDeoptOp& op) {
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const PhiOp& op) {
  if (current_input_block->IsLoop()) {
    DCHECK_EQ(op.input_count, 2);
    Node* input = GetNode(op.inputs()[0]);
    Node* node = AddNode(common.Phi(op.rep, 2), {input, input});
    loop_phis.emplace_back(node, op.inputs()[1]);
    return node;
  } else {
    base::SmallVector<Node*, 8> inputs;
    for (OpIndex i : op.inputs()) {
      inputs.push_back(GetNode(i));
    }
    inputs.push_back(tf_graph->start());
    return AddNode(common.Phi(op.rep, op.input_count), base::VectorOf(inputs));
  }
}
Node* ScheduleBuilder::ProcessOperation(const CheckpointOp& op) {
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const CallOp& op) {
  base::SmallVector<Node*, 16> inputs;
  inputs.push_back(GetNode(op.callee()));
  for (OpIndex i : op.arguments()) {
    inputs.push_back(GetNode(i));
  }
  return AddNode(common.Call(op.descriptor), base::VectorOf(inputs));
}
Node* ScheduleBuilder::ProcessOperation(const ReturnOp& op) {
  Node* pop_count = AddNode(common.Int32Constant(op.pop_count), {});
  base::SmallVector<Node*, 8> inputs = base::make_array(pop_count);
  for (OpIndex i : op.return_values()) {
    inputs.push_back(GetNode(i));
  }
  Node* node =
      MakeNode(common.Return(static_cast<int>(op.return_values().size())),
               base::VectorOf(inputs));
  schedule->AddReturn(current_block, node);
  current_block = nullptr;
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const BranchOp& op) {
  Node* branch =
      MakeNode(common.Branch(BranchHint::kNone), {GetNode(op.condition())});
  BasicBlock* true_block = GetBlock(*op.if_true);
  BasicBlock* false_block = GetBlock(*op.if_false);
  schedule->AddBranch(current_block, branch, true_block, false_block);
  true_block->AddNode(MakeNode(common.IfTrue(), {branch}));
  false_block->AddNode(MakeNode(common.IfFalse(), {branch}));
  current_block = nullptr;
  return nullptr;
}

}  // namespace

RecreateScheduleResult RecreateSchedule(const Graph& graph,
                                        CallDescriptor* call_descriptor,
                                        Zone* zone) {
  return ScheduleBuilder{graph, call_descriptor, zone}.Run();
}

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8
