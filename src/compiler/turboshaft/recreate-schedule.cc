// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/recreate-schedule.h"

#include <initializer_list>
#include <limits>

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
  Node* target_parameter = nullptr;
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
  Node* IntPtrShl(Node* a, Node* b) {
    return AddNode(machine.Is64() ? machine.Word64Shl() : machine.Word32Shl(),
                   {a, b});
  }
  void ProcessOperation(const Operation& op);
#define DECL_PROCESS_OPERATION(Name) Node* ProcessOperation(const Name##Op& op);
  TURBOSHAFT_OPERATION_LIST(DECL_PROCESS_OPERATION)
#undef DECL_PROCESS_OPERATION

  std::pair<Node*, MachineType> BuildDeoptInput(FrameStateData::Iterator* it);
  Node* BuildStateValues(FrameStateData::Iterator* it, int32_t size);
  Node* BuildTaggedInput(FrameStateData::Iterator* it);
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
  DCHECK_EQ(blocks.size(), input.block_count());
  size_t param_count = call_descriptor->ParameterCount();
  tf_graph->SetStart(
      tf_graph->NewNode(common.Start(static_cast<int>(param_count) + 1)));
  if (call_descriptor->IsJSFunctionCall()) {
    target_parameter =
        AddNode(common.Parameter(Linkage::kJSCallClosureParamIndex),
                {tf_graph->start()});
  }
  for (size_t i = 0; i < param_count; ++i) {
    parameters.push_back(
        AddNode(common.Parameter(static_cast<int>(i)), {tf_graph->start()}));
  }
  tf_graph->SetEnd(tf_graph->NewNode(common.End(0)));

  for (const Block& block : input.blocks()) {
    current_input_block = &block;
    current_block = GetBlock(block);
    current_block->set_deferred(current_input_block->deferred);
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
  DCHECK_EQ(schedule->rpo_order()->size(), blocks.size());
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

Node* ScheduleBuilder::ProcessOperation(const BinaryOp& op) {
  const Operator* o;
  switch (op.rep) {
    case MachineRepresentation::kWord32:
      switch (op.kind) {
        case BinaryOp::Kind::kAdd:
          o = op.with_overflow_bit ? machine.Int32AddWithOverflow()
                                   : machine.Int32Add();
          break;
        case BinaryOp::Kind::kSub:
          o = op.with_overflow_bit ? machine.Int32SubWithOverflow()
                                   : machine.Int32Sub();
          break;
        case BinaryOp::Kind::kMul:
          o = op.with_overflow_bit ? machine.Int32MulWithOverflow()
                                   : machine.Int32Mul();
          break;
        case BinaryOp::Kind::kBitwiseAnd:
          DCHECK(!op.with_overflow_bit);
          o = machine.Word32And();
          break;
        case BinaryOp::Kind::kBitwiseOr:
          DCHECK(!op.with_overflow_bit);
          o = machine.Word32Or();
          break;
        case BinaryOp::Kind::kBitwiseXor:
          DCHECK(!op.with_overflow_bit);
          o = machine.Word32Xor();
          break;
      }
      break;
    case MachineRepresentation::kWord64:
      switch (op.kind) {
        case BinaryOp::Kind::kAdd:
          o = op.with_overflow_bit ? machine.Int64AddWithOverflow()
                                   : machine.Int64Add();
          break;
        case BinaryOp::Kind::kSub:
          o = op.with_overflow_bit ? machine.Int64SubWithOverflow()
                                   : machine.Int64Sub();
          break;
        case BinaryOp::Kind::kMul:
          DCHECK(!op.with_overflow_bit);
          o = machine.Int64Mul();
          break;
        case BinaryOp::Kind::kBitwiseAnd:
          DCHECK(!op.with_overflow_bit);
          o = machine.Word64And();
          break;
        case BinaryOp::Kind::kBitwiseOr:
          DCHECK(!op.with_overflow_bit);
          o = machine.Word64Or();
          break;
        case BinaryOp::Kind::kBitwiseXor:
          DCHECK(!op.with_overflow_bit);
          o = machine.Word64Xor();
          break;
      }
      break;
    case MachineRepresentation::kFloat32:
      DCHECK(!op.with_overflow_bit);
      switch (op.kind) {
        case BinaryOp::Kind::kAdd:
          o = machine.Float32Add();
          break;
        case BinaryOp::Kind::kSub:
          o = machine.Float32Sub();
          break;
        case BinaryOp::Kind::kMul:
          o = machine.Float32Mul();
          break;
        case BinaryOp::Kind::kBitwiseAnd:
        case BinaryOp::Kind::kBitwiseOr:
        case BinaryOp::Kind::kBitwiseXor:
          UNREACHABLE();
      }
      break;
    case MachineRepresentation::kFloat64:
      DCHECK(!op.with_overflow_bit);
      switch (op.kind) {
        case BinaryOp::Kind::kAdd:
          o = machine.Float64Add();
          break;
        case BinaryOp::Kind::kSub:
          o = machine.Float64Sub();
          break;
        case BinaryOp::Kind::kMul:
          o = machine.Float64Mul();
          break;
        case BinaryOp::Kind::kBitwiseAnd:
        case BinaryOp::Kind::kBitwiseOr:
        case BinaryOp::Kind::kBitwiseXor:
          UNREACHABLE();
      }
      break;
    default:
      UNREACHABLE();
  }
  Node* node = AddNode(o, {GetNode(op.left()), GetNode(op.right())});
  if (op.with_overflow_bit) {
    node = AddNode(common.Projection(0), {node});
  }
  return node;
}
Node* ScheduleBuilder::ProcessOperation(const FloatUnaryOp& op) {
  const Operator* o;
  switch (op.kind) {
    case FloatUnaryOp::Kind::kAbs:
      switch (op.rep) {
        case MachineRepresentation::kFloat32:
          o = machine.Float32Abs();
          break;
        case MachineRepresentation::kFloat64:
          o = machine.Float64Abs();
          break;
        default:
          UNREACHABLE();
      }
      break;
    case FloatUnaryOp::Kind::kNegate:
      switch (op.rep) {
        case MachineRepresentation::kFloat32:
          o = machine.Float32Neg();
          break;
        case MachineRepresentation::kFloat64:
          o = machine.Float64Neg();
          break;
        default:
          UNREACHABLE();
      }
      break;
    case FloatUnaryOp::Kind::kSilenceNaN:
      DCHECK_EQ(op.rep, MachineRepresentation::kFloat64);
      o = machine.Float64SilenceNaN();
      break;
  }
  return AddNode(o, {GetNode(op.input())});
}
Node* ScheduleBuilder::ProcessOperation(const ShiftOp& op) {
  const Operator* o;
  switch (op.rep) {
    case MachineRepresentation::kWord32:
      switch (op.kind) {
        case ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros:
          o = machine.Word32SarShiftOutZeros();
          break;
        case ShiftOp::Kind::kShiftRightArithmetic:
          o = machine.Word32Sar();
          break;
        case ShiftOp::Kind::kShiftRightLogical:
          o = machine.Word32Shr();
          break;
        case ShiftOp::Kind::kShiftLeft:
          o = machine.Word32Shl();
          break;
      }
      break;
    case MachineRepresentation::kWord64:
      switch (op.kind) {
        case ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros:
          o = machine.Word64SarShiftOutZeros();
          break;
        case ShiftOp::Kind::kShiftRightArithmetic:
          o = machine.Word64Sar();
          break;
        case ShiftOp::Kind::kShiftRightLogical:
          o = machine.Word64Shr();
          break;
        case ShiftOp::Kind::kShiftLeft:
          o = machine.Word64Shl();
          break;
      }
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
Node* ScheduleBuilder::ProcessOperation(const ComparisonOp& op) {
  const Operator* o;
  switch (op.rep) {
    case MachineRepresentation::kWord32:
      switch (op.kind) {
        case ComparisonOp::Kind::kSignedLessThan:
          o = machine.Int32LessThan();
          break;
        case ComparisonOp::Kind::kSignedLessThanOrEqual:
          o = machine.Int32LessThanOrEqual();
          break;
        case ComparisonOp::Kind::kUnsignedLessThan:
          o = machine.Uint32LessThan();
          break;
        case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
          o = machine.Uint32LessThanOrEqual();
          break;
      }
      break;
    case MachineRepresentation::kWord64:
      switch (op.kind) {
        case ComparisonOp::Kind::kSignedLessThan:
          o = machine.Int64LessThan();
          break;
        case ComparisonOp::Kind::kSignedLessThanOrEqual:
          o = machine.Int64LessThanOrEqual();
          break;
        case ComparisonOp::Kind::kUnsignedLessThan:
          o = machine.Uint64LessThan();
          break;
        case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
          o = machine.Uint64LessThanOrEqual();
          break;
      }
      break;
    case MachineRepresentation::kFloat32:
      switch (op.kind) {
        case ComparisonOp::Kind::kSignedLessThan:
          o = machine.Float32LessThan();
          break;
        case ComparisonOp::Kind::kSignedLessThanOrEqual:
          o = machine.Float32LessThanOrEqual();
          break;
        case ComparisonOp::Kind::kUnsignedLessThan:
        case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
          UNREACHABLE();
      }
      break;
    case MachineRepresentation::kFloat64:
      switch (op.kind) {
        case ComparisonOp::Kind::kSignedLessThan:
          o = machine.Float64LessThan();
          break;
        case ComparisonOp::Kind::kSignedLessThanOrEqual:
          o = machine.Float64LessThanOrEqual();
          break;
        case ComparisonOp::Kind::kUnsignedLessThan:
        case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
          UNREACHABLE();
      }
      break;
    default:
      UNREACHABLE();
  }
  return AddNode(o, {GetNode(op.left()), GetNode(op.right())});
}
Node* ScheduleBuilder::ProcessOperation(const ChangeOp& op) {
  const Operator* o;
  switch (op.kind) {
    using Kind = ChangeOp::Kind;
    case Kind::kIntegerTruncate:
      if (op.from == MachineRepresentation::kWord64 &&
          op.to == MachineRepresentation::kWord32) {
        o = machine.TruncateInt64ToInt32();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kSignedFloatTruncate:
      if (op.from == MachineRepresentation::kFloat64 &&
          op.to == MachineRepresentation::kWord64) {
        o = machine.TruncateFloat64ToInt64(TruncateKind::kArchitectureDefault);
      } else if (op.from == MachineRepresentation::kFloat64 &&
                 op.to == MachineRepresentation::kWord32) {
        o = machine.RoundFloat64ToInt32();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kSignedFloatTruncateOverflowToMin:
      if (op.from == MachineRepresentation::kFloat64 &&
          op.to == MachineRepresentation::kWord64) {
        o = machine.TruncateFloat64ToInt64(TruncateKind::kSetOverflowToMin);
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kUnsignedFloatTruncate:
      if (op.from == MachineRepresentation::kFloat64 &&
          op.to == MachineRepresentation::kWord32) {
        o = machine.TruncateFloat64ToWord32();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kExtractHighHalf:
      DCHECK_EQ(op.from, MachineRepresentation::kFloat64);
      DCHECK_EQ(op.to, MachineRepresentation::kWord32);
      o = machine.Float64ExtractHighWord32();
      break;
    case Kind::kExtractLowHalf:
      DCHECK_EQ(op.from, MachineRepresentation::kFloat64);
      DCHECK_EQ(op.to, MachineRepresentation::kWord32);
      o = machine.Float64ExtractLowWord32();
      break;
    case Kind::kBitcast:
      if (op.from == MachineRepresentation::kWord32 &&
          op.to == MachineRepresentation::kWord64) {
        o = machine.BitcastWord32ToWord64();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kSignExtend:
      if (op.from == MachineRepresentation::kWord32 &&
          op.to == MachineRepresentation::kFloat64) {
        o = machine.ChangeInt32ToFloat64();
      } else if (op.from == MachineRepresentation::kWord64 &&
                 op.to == MachineRepresentation::kFloat64) {
        o = machine.ChangeInt64ToFloat64();
      } else if (op.from == MachineRepresentation::kWord32 &&
                 op.to == MachineRepresentation::kWord64) {
        o = machine.ChangeInt32ToInt64();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kZeroExtend:
      if (op.from == MachineRepresentation::kWord32 &&
          op.to == MachineRepresentation::kWord64) {
        o = machine.ChangeUint32ToUint64();
      } else if (op.from == MachineRepresentation::kWord32 &&
                 op.to == MachineRepresentation::kFloat64) {
        o = machine.ChangeUint32ToFloat64();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kSignedNarrowing:
      if (op.from == MachineRepresentation::kFloat64 &&
          op.to == MachineRepresentation::kWord64) {
        o = machine.ChangeFloat64ToInt64();
      }
      if (op.from == MachineRepresentation::kFloat64 &&
          op.to == MachineRepresentation::kWord32) {
        o = machine.ChangeFloat64ToInt32();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kUnsignedNarrowing:
      if (op.from == MachineRepresentation::kFloat64 &&
          op.to == MachineRepresentation::kWord64) {
        o = machine.ChangeFloat64ToUint64();
      }
      if (op.from == MachineRepresentation::kFloat64 &&
          op.to == MachineRepresentation::kWord32) {
        o = machine.ChangeFloat64ToUint32();
      } else {
        UNIMPLEMENTED();
      }
      break;
  }
  return AddNode(o, {GetNode(op.input())});
}
Node* ScheduleBuilder::ProcessOperation(const TaggedBitcastOp& op) {
  const Operator* o;
  if (op.from == MachineRepresentation::kTagged &&
      op.to == MachineType::PointerRepresentation()) {
    o = machine.BitcastTaggedToWord();
  } else if (op.from == MachineType::PointerRepresentation() &&
             op.to == MachineRepresentation::kTagged) {
    o = machine.BitcastWordToTagged();
  } else {
    UNIMPLEMENTED();
  }
  return AddNode(o, {GetNode(op.input())});
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
      return AddNode(common.Int32Constant(static_cast<int32_t>(op.word32())),
                     {});
    case ConstantOp::Kind::kWord64:
      return AddNode(common.Int64Constant(static_cast<int32_t>(op.word64())),
                     {});
    case ConstantOp::Kind::kExternal:
      return AddNode(common.ExternalConstant(op.external_reference()), {});
    case ConstantOp::Kind::kHeapObject:
      return AddNode(common.HeapConstant(op.handle()), {});
    case ConstantOp::Kind::kCompressedHeapObject:
      return AddNode(common.CompressedHeapConstant(op.handle()), {});
    case ConstantOp::Kind::kNumber:
      return AddNode(common.NumberConstant(op.number()), {});
    case ConstantOp::Kind::kTaggedIndex:
      return AddNode(common.TaggedIndexConstant(op.tagged_index()), {});
    case ConstantOp::Kind::kFloat64:
      return AddNode(common.Float64Constant(op.float64()), {});
    case ConstantOp::Kind::kFloat32:
      return AddNode(common.Float32Constant(op.float32()), {});
    case ConstantOp::Kind::kDelayedString:
      return AddNode(common.DelayedStringConstant(op.delayed_string()), {});
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
Node* ScheduleBuilder::ProcessOperation(const IndexedLoadOp& op) {
  intptr_t offset = op.offset;
  if (op.kind == IndexedLoadOp::Kind::kOnHeap) {
    CHECK(offset >= std::numeric_limits<int32_t>::min() + kHeapObjectTag);
    offset -= kHeapObjectTag;
  }
  Node* base = GetNode(op.base());
  Node* index = GetNode(op.index());
  if (op.element_scale != 0) {
    index = IntPtrShl(index, IntPtrConstant(op.element_scale));
  }
  if (offset != 0) {
    index = IntPtrAdd(index, IntPtrConstant(offset));
  }
  return AddNode(machine.Load(op.loaded_rep), {base, index});
}
Node* ScheduleBuilder::ProcessOperation(const StoreOp& op) {
  intptr_t offset = op.offset;
  if (op.kind == StoreOp::Kind::kOnHeap) {
    CHECK(offset >= std::numeric_limits<int32_t>::min() + kHeapObjectTag);
    offset -= kHeapObjectTag;
  }
  Node* base = GetNode(op.base());
  Node* value = GetNode(op.value());
  return AddNode(
      machine.Store(StoreRepresentation(op.stored_rep, op.write_barrier)),
      {base, IntPtrConstant(offset), value});
}
Node* ScheduleBuilder::ProcessOperation(const IndexedStoreOp& op) {
  intptr_t offset = op.offset;
  if (op.kind == IndexedStoreOp::Kind::kOnHeap) {
    CHECK(offset >= std::numeric_limits<int32_t>::min() + kHeapObjectTag);
    offset -= kHeapObjectTag;
  }
  Node* base = GetNode(op.base());
  Node* index = GetNode(op.index());
  Node* value = GetNode(op.value());
  if (op.element_scale != 0) {
    index = IntPtrShl(index, IntPtrConstant(op.element_scale));
  }
  if (offset != 0) {
    index = IntPtrAdd(index, IntPtrConstant(offset));
  }
  return AddNode(
      machine.Store(StoreRepresentation(op.stored_rep, op.write_barrier)),
      {base, index, value});
}
Node* ScheduleBuilder::ProcessOperation(const ParameterOp& op) {
  if (op.parameter_index == Linkage::kJSCallClosureParamIndex) {
    return target_parameter;
  }
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
  Node* call = GetNode(op.call());
  Node* frame_state = GetNode(op.frame_state());
  call->AppendInput(zone, frame_state);
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const DeoptimizeIfOp& op) {
  Node* condition = GetNode(op.condition());
  Node* frame_state = GetNode(op.frame_state());
  const Operator* o =
      op.negated
          ? common.DeoptimizeUnless(op.parameters->kind(),
                                    op.parameters->reason(),
                                    op.parameters->feedback())
          : common.DeoptimizeIf(op.parameters->kind(), op.parameters->reason(),
                                op.parameters->feedback());
  return AddNode(o, {condition, frame_state});
}
Node* ScheduleBuilder::ProcessOperation(const DeoptimizeOp& op) {
  Node* frame_state = GetNode(op.frame_state());
  const Operator* o =
      common.Deoptimize(op.parameters->kind(), op.parameters->reason(),
                        op.parameters->feedback());
  Node* node = MakeNode(o, {frame_state});
  schedule->AddDeoptimize(current_block, node);
  current_block = nullptr;
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
Node* ScheduleBuilder::ProcessOperation(const ProjectionOp& op) {
  switch (op.kind) {
    case ProjectionOp::Kind::kOverflowBit: {
      Node* projection = GetNode(op.input());
      DCHECK(projection->opcode() == IrOpcode::kProjection &&
             ProjectionIndexOf(projection->op()) == 0);
      return AddNode(common.Projection(1), {projection->InputAt(0)});
    }
  }
}

constexpr int32_t kMaxStateValueInputCount = 8;

std::pair<Node*, MachineType> ScheduleBuilder::BuildDeoptInput(
    FrameStateData::Iterator* it) {
  switch (it->current_instr()) {
    using Instr = FrameStateData::Instr;
    case Instr::kInput: {
      MachineType type;
      OpIndex input;
      it->ConsumeInput(&type, &input);
      return {GetNode(input), type};
    }
    case Instr::kDematerializedObject: {
      uint32_t obj_id;
      uint32_t field_count;
      it->ConsumeDematerializedObject(&obj_id, &field_count);
      base::SmallVector<Node*, 16> fields;
      ZoneVector<MachineType>& field_types =
          *tf_graph->zone()->New<ZoneVector<MachineType>>(field_count,
                                                          tf_graph->zone());
      for (uint32_t i = 0; i < field_count; ++i) {
        std::pair<Node*, MachineType> p = BuildDeoptInput(it);
        fields.push_back(p.first);
        field_types[i] = p.second;
      }
      return {AddNode(common.TypedObjectState(obj_id, &field_types),
                      base::VectorOf(fields)),
              MachineType::TaggedPointer()};
    }
    case Instr::kDematerializedObjectReference: {
      uint32_t obj_id;
      it->ConsumeDematerializedObjectReference(&obj_id);
      return {AddNode(common.ObjectId(obj_id), {}),
              MachineType::TaggedPointer()};
    }
    case Instr::kUnusedRegister:
      UNREACHABLE();
  }
}

Node* ScheduleBuilder::BuildStateValues(FrameStateData::Iterator* it,
                                        int32_t size) {
  base::SmallVector<Node*, kMaxStateValueInputCount> inputs;
  base::SmallVector<MachineType, kMaxStateValueInputCount> types;
  SparseInputMask::BitMaskType input_mask = 0;
  int32_t child_size =
      (size + kMaxStateValueInputCount - 1) / kMaxStateValueInputCount;
  int32_t mask_size = 0;
  for (int32_t i = 0; i < size; ++i) {
    ++mask_size;
    if (size <= kMaxStateValueInputCount) {
      if (it->current_instr() == FrameStateData::Instr::kUnusedRegister) {
        it->ConsumeUnusedRegister();
      } else {
        std::pair<Node*, MachineType> p = BuildDeoptInput(it);
        input_mask |= SparseInputMask::BitMaskType{1} << i;
        inputs.push_back(p.first);
        types.push_back(p.second);
      }

    } else {
      input_mask |= SparseInputMask::BitMaskType{1} << i;
      inputs.push_back(BuildStateValues(it, std::min(child_size, size - i)));
      // This is a dummy type that shouldn't matter.
      types.push_back(MachineType::AnyTagged());
      size = size - child_size + 1;
    }
  }
  input_mask |= SparseInputMask::kEndMarker << mask_size;
  return AddNode(common.TypedStateValues(zone->New<ZoneVector<MachineType>>(
                                             types.begin(), types.end(), zone),
                                         SparseInputMask(input_mask)),
                 base::VectorOf(inputs));
}

Node* ScheduleBuilder::BuildTaggedInput(FrameStateData::Iterator* it) {
  std::pair<Node*, MachineType> p = BuildDeoptInput(it);
  DCHECK(p.second.IsTagged());
  return p.first;
}

Node* ScheduleBuilder::ProcessOperation(const FrameStateOp& op) {
  const FrameStateInfo& info = op.data->frame_state_info;
  auto it = op.data->iterator(op.state_values());

  Node* parameter_state_values = BuildStateValues(&it, info.parameter_count());
  Node* register_state_values = BuildStateValues(&it, info.local_count());
  Node* accumulator_state_values = BuildStateValues(&it, info.stack_count());
  Node* context = BuildTaggedInput(&it);
  Node* closure = BuildTaggedInput(&it);
  Node* parent =
      op.inlined ? GetNode(op.parent_frame_state()) : tf_graph->start();

  return AddNode(common.FrameState(info.bailout_id(), info.state_combine(),
                                   info.function_info()),
                 {parameter_state_values, register_state_values,
                  accumulator_state_values, context, closure, parent});
}
Node* ScheduleBuilder::ProcessOperation(const CallOp& op) {
  base::SmallVector<Node*, 16> inputs;
  inputs.push_back(GetNode(op.callee()));
  for (OpIndex i : op.arguments()) {
    inputs.push_back(GetNode(i));
  }
  return AddNode(common.Call(op.descriptor), base::VectorOf(inputs));
}
Node* ScheduleBuilder::ProcessOperation(const UnreachableOp& op) {
  Node* node = MakeNode(common.Throw(), {});
  schedule->AddThrow(current_block, node);
  current_block = nullptr;
  return nullptr;
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
Node* ScheduleBuilder::ProcessOperation(const SwitchOp& op) {
  size_t succ_count = op.cases.size() + 1;
  Node* switch_node =
      MakeNode(common.Switch(succ_count), {GetNode(op.input())});

  base::SmallVector<BasicBlock*, 16> successors;
  for (SwitchOp::Case c : op.cases) {
    BasicBlock* case_block = GetBlock(*c.destination);
    successors.push_back(case_block);
    Node* case_node = MakeNode(common.IfValue(c.value), {switch_node});
    schedule->AddNode(case_block, case_node);
  }
  BasicBlock* default_block = GetBlock(*op.default_case);
  successors.push_back(default_block);
  schedule->AddNode(default_block, MakeNode(common.IfDefault(), {switch_node}));

  schedule->AddSwitch(current_block, switch_node, successors.data(),
                      successors.size());
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
