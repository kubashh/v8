// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_H_
#define V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_H_

#include <map>

#include "src/codegen/cpu-features.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/backend/instruction-scheduler.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/utils/bit-vector.h"
#include "src/zone/zone-containers.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/simd-shuffle.h"
#endif  // V8_ENABLE_WEBASSEMBLY

// TODO(nicohartmann@): Remove thonse once Adapters have their own files.
#include "src/compiler/schedule.h"
#include "src/compiler/turboshaft/graph.h"

#define DECLARE_UNREACHABLE_TURBOSHAFT_FALLBACK(ret, name)                \
  template <typename... Args>                                             \
  std::enable_if_t<                                                       \
      Adapter::IsTurboshaft && (... || std::is_same_v<Args, Node*>), ret> \
  name(Args...) {                                                         \
    UNREACHABLE();                                                        \
  }

namespace v8 {
namespace internal {

class TickCounter;

namespace compiler {

// Forward declarations.
class BasicBlock;
template <typename Adapter>
struct CallBufferT;  // TODO(bmeurer): Remove this.
class Linkage;
template <typename Adapter>
class OperandGeneratorT;
class SwitchInfo;
class StateObjectDeduplicator;

constexpr bool HasTurboshaftSupport(IrOpcode::Value opcode) { return false; }

struct TurbofanAdapter {
  static constexpr bool IsTurbofan = true;
  static constexpr bool IsTurboshaft = false;
  using schedule_t = Schedule*;
  using block_t = BasicBlock*;
  using block_range_t = ZoneVector<block_t>;
  using node_t = Node*;
  using inputs_t = Node::Inputs;
  using opcode_t = IrOpcode::Value;
  using id_t = uint32_t;

  class CallView {
   public:
    explicit CallView(node_t node) : node_(node) {
      DCHECK(node_->opcode() == IrOpcode::kCall ||
             node_->opcode() == IrOpcode::kTailCall);
    }

    int return_count() const { return node_->op()->ValueOutputCount(); }
    node_t callee() const { return node_->InputAt(0); }
    node_t frame_state() const {
      const CallDescriptor* descriptor = CallDescriptorOf(node_->op());
      return node_->InputAt(static_cast<int>(descriptor->InputCount()));
    }
    base::Vector<node_t> arguments() const {
      base::Vector<node_t> inputs = node_->inputs_vector();
      return inputs.SubVector(1, inputs.size());
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  class BranchView {
   public:
    explicit BranchView(node_t node) : node_(node) {
      DCHECK(node_->opcode() == IrOpcode::kBranch);
    }

    node_t condition() const { return node_->InputAt(0); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  class WordBinopView {
   public:
    explicit WordBinopView(node_t node) : node_(node), m_(node) {
      //      DCHECK(node_->opcode() == IrOpcode::kInt32Add ||
      //             node_->opcode() == IrOpcode::kInt32Sub ||
      //             node_->opcode() == IrOpcode::kWord32And ||
      //             node_->opcode() == IrOpcode::kWord32Or ||
      //             node_->opcode() == IrOpcode::kInt64Add ||
      //             node_->opcode() == IrOpcode::kInt64Sub ||
      //             node_->opcode() == IrOpcode::kWord64And ||
      //             node_->opcode() == IrOpcode::kWord64Or);
    }

    void EnsureConstantIsRightIfCommutative() {
      // Nothing to do. Matcher already ensures that.
    }

    node_t left() const { return m_.left().node(); }
    node_t right() const { return m_.right().node(); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    Int32BinopMatcher m_;
  };

  class LoadView {
   public:
    explicit LoadView(node_t node) : node_(node) {
      DCHECK(node_->opcode() == IrOpcode::kLoad);
    }

    LoadRepresentation loaded_rep() const {
      return LoadRepresentationOf(node_->op());
    }

    operator node_t() const { return node_; }

   private:
    node_t node_;
  };

  CallView call_view(node_t node) { return CallView{node}; }
  BranchView branch_view(node_t node) { return BranchView(node); }
  WordBinopView word_binop_view(node_t node) { return WordBinopView(node); }
  LoadView load_view(node_t node) { return LoadView(node); }

  void SetSchedule(schedule_t schedule) {}

  block_t block(schedule_t schedule, node_t node) const {
    return schedule->block(node);
  }

  RpoNumber rpo_number(block_t block) const {
    return RpoNumber::FromInt(block->rpo_number());
  }

  const block_range_t& rpo_order(schedule_t schedule) const {
    return *schedule->rpo_order();
  }

  bool IsLoopHeader(block_t block) const { return block->IsLoopHeader(); }

  size_t PredecessorCount(block_t block) const {
    return block->PredecessorCount();
  }
  block_t PredecessorAt(block_t block, size_t index) const {
    return block->PredecessorAt(index);
  }

  base::iterator_range<NodeVector::iterator> nodes(block_t block) {
    return {block->begin(), block->end()};
  }

  bool IsPhi(node_t node) const { return node->opcode() == IrOpcode::kPhi; }
  bool IsRetain(node_t node) const {
    return node->opcode() == IrOpcode::kRetain;
  }
  bool IsHeapConstant(node_t node) const {
    return node->opcode() == IrOpcode::kHeapConstant;
  }
  bool IsExternalConstant(node_t node) const {
    return node->opcode() == IrOpcode::kExternalConstant;
  }
  bool IsRelocatableWasmConstant(node_t node) const {
    return node->opcode() == IrOpcode::kRelocatableInt32Constant ||
           node->opcode() == IrOpcode::kRelocatableInt64Constant;
  }
  bool IsLoadOrLoadImmutable(node_t node) const {
    return node->opcode() == IrOpcode::kLoad ||
           node->opcode() == IrOpcode::kLoadImmutable;
  }

  node_t input_at(node_t node, size_t index) const {
    return node->InputAt(static_cast<int>(index));
  }
  inputs_t inputs(node_t node) const { return node->inputs(); }
  opcode_t opcode(node_t node) const { return node->opcode(); }
  bool is_exclusive_user_of(node_t user, node_t value) const {
    for (Edge const edge : value->use_edges()) {
      if (edge.from() != user && NodeProperties::IsValueEdge(edge)) {
        return false;
      }
    }
    return true;
  }

  id_t id(node_t node) const { return node->id(); }
  bool valid(node_t node) const { return node != nullptr; }

  node_t block_terminator(block_t block) const {
    return block->control_input();
  }
  node_t parent_frame_state(node_t node) const {
    DCHECK(node->opcode() == IrOpcode::kFrameState);
    return NodeProperties::GetFrameStateInput(node);
  }

  bool IsRequiredWhenUnused(node_t node) const {
    return !node->op()->HasProperty(Operator::kEliminatable);
  }
  bool IsCommutative(node_t node) const {
    return node->op()->HasProperty(Operator::kCommutative);
  }
};

struct TurboshaftAdapter {
  static constexpr bool IsTurbofan = false;
  static constexpr bool IsTurboshaft = true;
  using schedule_t = turboshaft::Graph*;
  using block_t = turboshaft::Block*;
  using block_range_t = ZoneVector<block_t>;
  using node_t = turboshaft::OpIndex;
  using inputs_t = base::Vector<const node_t>;
  using opcode_t = turboshaft::Opcode;
  using id_t = uint32_t;

  class CallView {
   public:
    explicit CallView(turboshaft::Graph* graph, node_t node) : node_(node) {
      op_ = &graph->Get(node_).Cast<turboshaft::CallOp>();
    }

    int return_count() const {
      return static_cast<int>(op_->outputs_rep().size());
    }
    node_t callee() const { return op_->callee(); }
    node_t frame_state() const { return op_->frame_state(); }
    base::Vector<const node_t> arguments() const { return op_->arguments(); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::CallOp* op_;
  };

  class BranchView {
   public:
    explicit BranchView(turboshaft::Graph* graph, node_t node) : node_(node) {
      op_ = &graph->Get(node_).Cast<turboshaft::BranchOp>();
    }

    node_t condition() const { return op_->condition(); }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::BranchOp* op_;
  };

  class WordBinopView {
   public:
    explicit WordBinopView(turboshaft::Graph* graph, node_t node)
        : node_(node) {
      op_ = &graph->Get(node_).Cast<turboshaft::WordBinopOp>();
      left_ = op_->left();
      right_ = op_->right();
      can_put_constant_right_ =
          op_->IsCommutative(op_->kind) &&
          graph->Get(left_).Is<turboshaft::ConstantOp>() &&
          !graph->Get(right_).Is<turboshaft::ConstantOp>();
    }

    void EnsureConstantIsRightIfCommutative() {
      if (!can_put_constant_right_) {
        std::swap(left_, right_);
        can_put_constant_right_ = false;
      }
    }

    node_t left() const { return left_; }
    node_t right() const { return right_; }

    operator node_t() const { return node_; }

   private:
    node_t node_;
    const turboshaft::WordBinopOp* op_;
    node_t left_;
    node_t right_;
    bool can_put_constant_right_;
  };

  class LoadView {
   public:
    explicit LoadView(turboshaft::Graph* graph, node_t node)
        : graph_(graph), node_(node) {}

    LoadRepresentation loaded_rep() const {
      return graph_->Get(node_)
          .Cast<turboshaft::LoadOp>()
          .loaded_rep.ToMachineType();
    }

    operator node_t() const { return node_; }

   private:
    const turboshaft::Graph* graph_;
    node_t node_;
  };

  CallView call_view(node_t node) { return CallView{graph_, node}; }
  BranchView branch_view(node_t node) { return BranchView(graph_, node); }
  WordBinopView word_binop_view(node_t node) {
    return WordBinopView(graph_, node);
  }
  LoadView load_view(node_t node) { return LoadView(graph_, node); }

  void SetSchedule(schedule_t schedule) { graph_ = schedule; }
  turboshaft::Graph* turboshaft_graph() const { return graph_; }

  block_t block(schedule_t schedule, node_t node) const {
    return &schedule->Get(schedule->BlockOf(node));
  }

  RpoNumber rpo_number(block_t block) const {
    return RpoNumber::FromInt(block->index().id());
  }

  const block_range_t& rpo_order(schedule_t schedule) {
    return schedule->blocks_vector();
  }

  bool IsLoopHeader(block_t block) const { return block->IsLoop(); }

  size_t PredecessorCount(block_t block) const {
    return block->PredecessorCount();
  }
  block_t PredecessorAt(block_t block, size_t index) const {
    return block->Predecessors()[index];
  }

  base::iterator_range<turboshaft::Graph::OpIndexIterator> nodes(
      block_t block) {
    return graph_->OperationIndices(*block);
  }

  bool IsPhi(node_t node) const {
    return graph_->Get(node).Is<turboshaft::PhiOp>();
  }
  bool IsRetain(node_t node) const {
    return graph_->Get(node).Is<turboshaft::RetainOp>();
  }
  bool IsHeapConstant(node_t node) const {
    turboshaft::ConstantOp* constant =
        graph_->Get(node).TryCast<turboshaft::ConstantOp>();
    if (constant == nullptr) return false;
    return constant->kind == turboshaft::ConstantOp::Kind::kHeapObject;
  }
  bool IsExternalConstant(node_t node) const {
    turboshaft::ConstantOp* constant =
        graph_->Get(node).TryCast<turboshaft::ConstantOp>();
    if (constant == nullptr) return false;
    return constant->kind == turboshaft::ConstantOp::Kind::kExternal;
  }
  bool IsRelocatableWasmConstant(node_t node) const {
    using namespace turboshaft;
    ConstantOp* constant = graph_->Get(node).TryCast<ConstantOp>();
    if (constant == nullptr) return false;
    return constant->kind == any_of(ConstantOp::Kind::kRelocatableWasmCall,
                                    ConstantOp::Kind::kRelocatableWasmStubCall);
  }
  bool IsLoadOrLoadImmutable(node_t node) const {
    return graph_->Get(node).opcode == turboshaft::Opcode::kLoad;
  }

  node_t input_at(node_t node, size_t index) const {
    return graph_->Get(node).input(index);
  }
  inputs_t inputs(node_t node) const { return graph_->Get(node).inputs(); }
  opcode_t opcode(node_t node) const { return graph_->Get(node).opcode; }
  bool is_exclusive_user_of(node_t user, node_t value) const {
    DCHECK(valid(user));
    DCHECK(valid(value));
    const size_t use_count = base::count_if(
        graph_->Get(user).inputs(),
        [user](turboshaft::OpIndex input) { return input == user; });
    DCHECK_LT(0, use_count);
    DCHECK_LE(use_count, graph_->Get(value).saturated_use_count);
    return graph_->Get(value).saturated_use_count == use_count;
  }

  id_t id(node_t node) const { return node.id(); }
  bool valid(node_t node) const { return node.valid(); }

  node_t block_terminator(block_t block) const {
    return graph_->PreviousIndex(block->end());
  }
  node_t parent_frame_state(node_t node) const {
    const turboshaft::FrameStateOp& frame_state =
        graph_->Get(node).Cast<turboshaft::FrameStateOp>();
    return frame_state.parent_frame_state();
  }

  bool IsRequiredWhenUnused(node_t node) const {
    return graph_->Get(node).IsRequiredWhenUnused();
  }
  bool IsCommutative(node_t node) const {
    const turboshaft::Operation& op = graph_->Get(node);
    if (const auto binop = op.TryCast<turboshaft::WordBinopOp>()) {
      return turboshaft::WordBinopOp::IsCommutative(binop->kind);
    } else if (const auto binop =
                   op.TryCast<turboshaft::OverflowCheckedBinopOp>()) {
      return turboshaft::OverflowCheckedBinopOp::IsCommutative(binop->kind);
    } else if (const auto binop = op.TryCast<turboshaft::FloatBinopOp>()) {
      return turboshaft::FloatBinopOp::IsCommutative(binop->kind);
    }
    return false;
  }
  //  bool IsPure(node_t node) const {
  //    return graph_->Get(node).Effects().
  //  }

  // Temporary stubs
  block_t block(schedule_t, Node*) const { UNREACHABLE(); }
  RpoNumber rpo_number(BasicBlock*) const { UNREACHABLE(); }

 private:
  turboshaft::Graph* graph_;
};

// The flags continuation is a way to combine a branch or a materialization
// of a boolean value with an instruction that sets the flags register.
// The whole instruction is treated as a unit by the register allocator, and
// thus no spills or moves can be introduced between the flags-setting
// instruction and the branch or set it should be combined with.
template <typename Adapter>
class FlagsContinuationT final {
 public:
  using block_t = typename Adapter::block_t;
  using node_t = typename Adapter::node_t;
  using id_t = typename Adapter::id_t;

  FlagsContinuationT() : mode_(kFlags_none) {}

  // Creates a new flags continuation from the given condition and true/false
  // blocks.
  static FlagsContinuationT ForBranch(FlagsCondition condition,
                                      block_t true_block, block_t false_block) {
    return FlagsContinuationT(kFlags_branch, condition, true_block,
                              false_block);
  }

  // Creates a new flags continuation for an eager deoptimization exit.
  static FlagsContinuationT ForDeoptimize(FlagsCondition condition,
                                          DeoptimizeReason reason, id_t node_id,
                                          FeedbackSource const& feedback,
                                          node_t frame_state) {
    return FlagsContinuationT(kFlags_deoptimize, condition, reason, node_id,
                              feedback, frame_state);
  }
  static FlagsContinuationT ForDeoptimizeForTesting(
      FlagsCondition condition, DeoptimizeReason reason, id_t node_id,
      FeedbackSource const& feedback, node_t frame_state) {
    // test-instruction-scheduler.cc passes a dummy Node* as frame_state.
    // Contents don't matter as long as it's not nullptr.
    return FlagsContinuationT(kFlags_deoptimize, condition, reason, node_id,
                              feedback, frame_state);
  }

  // Creates a new flags continuation for a boolean value.
  static FlagsContinuationT ForSet(FlagsCondition condition, node_t result) {
    return FlagsContinuationT(condition, result);
  }

  // Creates a new flags continuation for a wasm trap.
  static FlagsContinuationT ForTrap(FlagsCondition condition, TrapId trap_id,
                                    id_t node_id, node_t frame_state) {
    return FlagsContinuationT(condition, trap_id, node_id, frame_state);
  }

  static FlagsContinuationT ForSelect(FlagsCondition condition, node_t result,
                                      node_t true_value, node_t false_value) {
    return FlagsContinuationT(condition, result, true_value, false_value);
  }

  bool IsNone() const { return mode_ == kFlags_none; }
  bool IsBranch() const { return mode_ == kFlags_branch; }
  bool IsDeoptimize() const { return mode_ == kFlags_deoptimize; }
  bool IsSet() const { return mode_ == kFlags_set; }
  bool IsTrap() const { return mode_ == kFlags_trap; }
  bool IsSelect() const { return mode_ == kFlags_select; }
  FlagsCondition condition() const {
    DCHECK(!IsNone());
    return condition_;
  }
  DeoptimizeReason reason() const {
    DCHECK(IsDeoptimize());
    return reason_;
  }
  id_t node_id() const {
    DCHECK(IsDeoptimize() || IsTrap());
    return node_id_;
  }
  FeedbackSource const& feedback() const {
    DCHECK(IsDeoptimize());
    return feedback_;
  }
  node_t frame_state() const {
    DCHECK(IsDeoptimize() || IsTrap());
    return frame_state_or_result_;
  }
  node_t result() const {
    DCHECK(IsSet() || IsSelect());
    return frame_state_or_result_;
  }
  TrapId trap_id() const {
    DCHECK(IsTrap());
    return trap_id_;
  }
  block_t true_block() const {
    DCHECK(IsBranch());
    return true_block_;
  }
  block_t false_block() const {
    DCHECK(IsBranch());
    return false_block_;
  }
  node_t true_value() const {
    DCHECK(IsSelect());
    return true_value_;
  }
  node_t false_value() const {
    DCHECK(IsSelect());
    return false_value_;
  }

  void Negate() {
    DCHECK(!IsNone());
    condition_ = NegateFlagsCondition(condition_);
  }

  void Commute() {
    DCHECK(!IsNone());
    condition_ = CommuteFlagsCondition(condition_);
  }

  void Overwrite(FlagsCondition condition) { condition_ = condition; }

  void OverwriteAndNegateIfEqual(FlagsCondition condition) {
    DCHECK(condition_ == kEqual || condition_ == kNotEqual);
    bool negate = condition_ == kEqual;
    condition_ = condition;
    if (negate) Negate();
  }

  void OverwriteUnsignedIfSigned() {
    switch (condition_) {
      case kSignedLessThan:
        condition_ = kUnsignedLessThan;
        break;
      case kSignedLessThanOrEqual:
        condition_ = kUnsignedLessThanOrEqual;
        break;
      case kSignedGreaterThan:
        condition_ = kUnsignedGreaterThan;
        break;
      case kSignedGreaterThanOrEqual:
        condition_ = kUnsignedGreaterThanOrEqual;
        break;
      default:
        break;
    }
  }

  // Encodes this flags continuation into the given opcode.
  InstructionCode Encode(InstructionCode opcode) {
    opcode |= FlagsModeField::encode(mode_);
    if (mode_ != kFlags_none) {
      opcode |= FlagsConditionField::encode(condition_);
    }
    return opcode;
  }

 private:
  FlagsContinuationT(FlagsMode mode, FlagsCondition condition,
                     block_t true_block, block_t false_block)
      : mode_(mode),
        condition_(condition),
        true_block_(true_block),
        false_block_(false_block) {
    DCHECK(mode == kFlags_branch);
    DCHECK_NOT_NULL(true_block);
    DCHECK_NOT_NULL(false_block);
  }

  FlagsContinuationT(FlagsMode mode, FlagsCondition condition,
                     DeoptimizeReason reason, id_t node_id,
                     FeedbackSource const& feedback, node_t frame_state)
      : mode_(mode),
        condition_(condition),
        reason_(reason),
        node_id_(node_id),
        feedback_(feedback),
        frame_state_or_result_(frame_state) {
    DCHECK(mode == kFlags_deoptimize);
    //    DCHECK_NOT_NULL(frame_state);
  }

  FlagsContinuationT(FlagsCondition condition, node_t result)
      : mode_(kFlags_set),
        condition_(condition),
        frame_state_or_result_(result) {
    //    DCHECK_NOT_NULL(result);
  }

  FlagsContinuationT(FlagsCondition condition, TrapId trap_id, id_t node_id,
                     node_t frame_state)
      : mode_(kFlags_trap),
        condition_(condition),
        node_id_(node_id),
        frame_state_or_result_(frame_state),
        trap_id_(trap_id) {}

  FlagsContinuationT(FlagsCondition condition, node_t result, node_t true_value,
                     node_t false_value)
      : mode_(kFlags_select),
        condition_(condition),
        frame_state_or_result_(result),
        true_value_(true_value),
        false_value_(false_value) {
    //    DCHECK_NOT_NULL(result);
    //    DCHECK_NOT_NULL(true_value);
    //    DCHECK_NOT_NULL(false_value);
  }

  FlagsMode const mode_;
  FlagsCondition condition_;
  DeoptimizeReason reason_;         // Only valid if mode_ == kFlags_deoptimize*
  id_t node_id_;                    // Only valid if mode_ == kFlags_deoptimize*
  FeedbackSource feedback_;         // Only valid if mode_ == kFlags_deoptimize*
  node_t frame_state_or_result_;    // Only valid if mode_ == kFlags_deoptimize*
                                    // or mode_ == kFlags_set.
  block_t true_block_;              // Only valid if mode_ == kFlags_branch*.
  block_t false_block_;             // Only valid if mode_ == kFlags_branch*.
  TrapId trap_id_;                  // Only valid if mode_ == kFlags_trap.
  node_t true_value_;               // Only valid if mode_ == kFlags_select.
  node_t false_value_;              // Only valid if mode_ == kFlags_select.
};

// This struct connects nodes of parameters which are going to be pushed on the
// call stack with their parameter index in the call descriptor of the callee.
template <typename Adapter>
struct PushParameterT {
  using node_t = typename Adapter::node_t;
  PushParameterT(node_t n = {},
                 LinkageLocation l = LinkageLocation::ForAnyRegister())
      : node(n), location(l) {}

  node_t node;
  LinkageLocation location;
};

enum class FrameStateInputKind { kAny, kStackSlot };

// Instruction selection generates an InstructionSequence for a given Schedule.
template <typename Adapter>
class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) InstructionSelectorT final
    : public Adapter {
 public:
  using OperandGenerator = OperandGeneratorT<Adapter>;
  using PushParameter = PushParameterT<Adapter>;
  using CallBuffer = CallBufferT<Adapter>;
  using FlagsContinuation = FlagsContinuationT<Adapter>;

  using schedule_t = typename Adapter::schedule_t;
  using block_t = typename Adapter::block_t;
  using block_range_t = typename Adapter::block_range_t;
  using node_t = typename Adapter::node_t;

  // Forward declarations.
  class Features;

  enum SourcePositionMode { kCallSourcePositions, kAllSourcePositions };
  enum EnableScheduling { kDisableScheduling, kEnableScheduling };
  enum EnableRootsRelativeAddressing {
    kDisableRootsRelativeAddressing,
    kEnableRootsRelativeAddressing
  };
  enum EnableSwitchJumpTable {
    kDisableSwitchJumpTable,
    kEnableSwitchJumpTable
  };
  enum EnableTraceTurboJson { kDisableTraceTurboJson, kEnableTraceTurboJson };

  InstructionSelectorT(
      Zone* zone, size_t node_count, Linkage* linkage,
      InstructionSequence* sequence, schedule_t schedule,
      SourcePositionTable* source_positions, Frame* frame,
      EnableSwitchJumpTable enable_switch_jump_table, TickCounter* tick_counter,
      JSHeapBroker* broker, size_t* max_unoptimized_frame_height,
      size_t* max_pushed_argument_count,
      SourcePositionMode source_position_mode = kCallSourcePositions,
      Features features = SupportedFeatures(),
      EnableScheduling enable_scheduling = v8_flags.turbo_instruction_scheduling
                                               ? kEnableScheduling
                                               : kDisableScheduling,
      EnableRootsRelativeAddressing enable_roots_relative_addressing =
          kDisableRootsRelativeAddressing,
      EnableTraceTurboJson trace_turbo = kDisableTraceTurboJson);

  // Visit code for the entire graph with the included schedule.
  base::Optional<BailoutReason> SelectInstructions();

  void StartBlock(RpoNumber rpo);
  void EndBlock(RpoNumber rpo);
  void AddInstruction(Instruction* instr);
  void AddTerminator(Instruction* instr);

  // ===========================================================================
  // ============= Architecture-independent code emission methods. =============
  // ===========================================================================

  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    size_t temp_count = 0, InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, size_t temp_count = 0,
                    InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, InstructionOperand b,
                    size_t temp_count = 0, InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, InstructionOperand b,
                    InstructionOperand c, size_t temp_count = 0,
                    InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, InstructionOperand b,
                    InstructionOperand c, InstructionOperand d,
                    size_t temp_count = 0, InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, InstructionOperand b,
                    InstructionOperand c, InstructionOperand d,
                    InstructionOperand e, size_t temp_count = 0,
                    InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, InstructionOperand output,
                    InstructionOperand a, InstructionOperand b,
                    InstructionOperand c, InstructionOperand d,
                    InstructionOperand e, InstructionOperand f,
                    size_t temp_count = 0, InstructionOperand* temps = nullptr);
  Instruction* Emit(InstructionCode opcode, size_t output_count,
                    InstructionOperand* outputs, size_t input_count,
                    InstructionOperand* inputs, size_t temp_count = 0,
                    InstructionOperand* temps = nullptr);
  Instruction* Emit(Instruction* instr);

  // [0-3] operand instructions with no output, uses labels for true and false
  // blocks of the continuation.
  Instruction* EmitWithContinuation(InstructionCode opcode,
                                    FlagsContinuation* cont);
  Instruction* EmitWithContinuation(InstructionCode opcode,
                                    InstructionOperand a,
                                    FlagsContinuation* cont);
  Instruction* EmitWithContinuation(InstructionCode opcode,
                                    InstructionOperand a, InstructionOperand b,
                                    FlagsContinuation* cont);
  Instruction* EmitWithContinuation(InstructionCode opcode,
                                    InstructionOperand a, InstructionOperand b,
                                    InstructionOperand c,
                                    FlagsContinuation* cont);
  Instruction* EmitWithContinuation(InstructionCode opcode, size_t output_count,
                                    InstructionOperand* outputs,
                                    size_t input_count,
                                    InstructionOperand* inputs,
                                    FlagsContinuation* cont);
  Instruction* EmitWithContinuation(
      InstructionCode opcode, size_t output_count, InstructionOperand* outputs,
      size_t input_count, InstructionOperand* inputs, size_t temp_count,
      InstructionOperand* temps, FlagsContinuation* cont);

  void EmitIdentity(Node* node);

  // ===========================================================================
  // ============== Architecture-independent CPU feature methods. ==============
  // ===========================================================================

  class Features final {
   public:
    Features() : bits_(0) {}
    explicit Features(unsigned bits) : bits_(bits) {}
    explicit Features(CpuFeature f) : bits_(1u << f) {}
    Features(CpuFeature f1, CpuFeature f2) : bits_((1u << f1) | (1u << f2)) {}

    bool Contains(CpuFeature f) const { return (bits_ & (1u << f)); }

   private:
    unsigned bits_;
  };

  bool IsSupported(CpuFeature feature) const {
    return features_.Contains(feature);
  }

  // Returns the features supported on the target platform.
  static Features SupportedFeatures() {
    return Features(CpuFeatures::SupportedFeatures());
  }

  // TODO(sigurds) This should take a CpuFeatures argument.
  static MachineOperatorBuilder::Flags SupportedMachineOperatorFlags();

  static MachineOperatorBuilder::AlignmentRequirements AlignmentRequirements();

  // ===========================================================================
  // ============ Architecture-independent graph covering methods. =============
  // ===========================================================================

  // Used in pattern matching during code generation.
  // Check if {node} can be covered while generating code for the current
  // instruction. A node can be covered if the {user} of the node has the only
  // edge, the two are in the same basic block, and there are no side-effects
  // in-between. The last check is crucial for soundness.
  // For pure nodes, CanCover(a,b) is checked to avoid duplicated execution:
  // If this is not the case, code for b must still be generated for other
  // users, and fusing is unlikely to improve performance.
  bool CanCover(node_t user, node_t node) const;
  template <typename T>
  bool CanCover(T*, T*) const {
    UNREACHABLE(/*REMOVE*/);
  }

  // Used in pattern matching during code generation.
  // This function checks that {node} and {user} are in the same basic block,
  // and that {user} is the only user of {node} in this basic block.  This
  // check guarantees that there are no users of {node} scheduled between
  // {node} and {user}, and thus we can select a single instruction for both
  // nodes, if such an instruction exists. This check can be used for example
  // when selecting instructions for:
  //   n = Int32Add(a, b)
  //   c = Word32Compare(n, 0, cond)
  //   Branch(c, true_label, false_label)
  // Here we can generate a flag-setting add instruction, even if the add has
  // uses in other basic blocks, since the flag-setting add instruction will
  // still generate the result of the addition and not just set the flags.
  // However, if we had uses of the add in the same basic block, we could have:
  //   n = Int32Add(a, b)
  //   o = OtherOp(n, ...)
  //   c = Word32Compare(n, 0, cond)
  //   Branch(c, true_label, false_label)
  // where we cannot select the add and the compare together.  If we were to
  // select a flag-setting add instruction for Word32Compare and Int32Add while
  // visiting Word32Compare, we would then have to select an instruction for
  // OtherOp *afterwards*, which means we would attempt to use the result of
  // the add before we have defined it.
  bool IsOnlyUserOfNodeInSameBlock(node_t user, node_t node) const;

  // Checks if {node} was already defined, and therefore code was already
  // generated for it.
  bool IsDefined(node_t node) const;
  template <typename T>
  bool IsDefined(T* node) const {
    UNREACHABLE(/*REMOVE*/);
  }

  // Checks if {node} has any uses, and therefore code has to be generated for
  // it.
  bool IsUsed(node_t node) const;
  template <typename T>
  bool IsUsed(T* node) const {
    UNREACHABLE(/*REMOVE*/);
  }

  // Checks if {node} is currently live.
  template <typename T>
  bool IsLive(T*) const {
    UNREACHABLE(/*REMOVE*/);
  }
  bool IsLive(node_t node) const { return !IsDefined(node) && IsUsed(node); }

  // Gets the effect level of {node}.
  template <typename T>
  int GetEffectLevel(T*) const {
    UNREACHABLE(/*REMOVE*/);
  }
  int GetEffectLevel(node_t node) const;

  // Gets the effect level of {node}, appropriately adjusted based on
  // continuation flags if the node is a branch.
  template <typename... Args>
  int GetEffectLevel(Args...) const {
    UNREACHABLE(/*REMOVE*/);
  }
  int GetEffectLevel(node_t node, FlagsContinuation* cont) const;

  int GetVirtualRegister(node_t node);
  template <typename T>
  int GetVirtualRegister(T*) {
    UNREACHABLE(/*REMOVE*/);
  }
  const std::map<NodeId, int> GetVirtualRegistersForTesting() const;

  // Check if we can generate loads and stores of ExternalConstants relative
  // to the roots register.
  bool CanAddressRelativeToRootsRegister(
      const ExternalReference& reference) const;
  // Check if we can use the roots register to access GC roots.
  bool CanUseRootsRegister() const;

  Isolate* isolate() const { return sequence()->isolate(); }

  const ZoneVector<std::pair<int, int>>& instr_origins() const {
    return instr_origins_;
  }

 private:
  friend class OperandGeneratorT<Adapter>;

  bool UseInstructionScheduling() const {
    return (enable_scheduling_ == kEnableScheduling) &&
           InstructionScheduler::SchedulerSupported();
  }

  void AppendDeoptimizeArguments(InstructionOperandVector* args,
                                 DeoptimizeReason reason, id_t node_id,
                                 FeedbackSource const& feedback,
                                 node_t frame_state,
                                 DeoptimizeKind kind = DeoptimizeKind::kEager);

  void EmitTableSwitch(const SwitchInfo& sw,
                       InstructionOperand const& index_operand);
  void EmitBinarySearchSwitch(const SwitchInfo& sw,
                              InstructionOperand const& value_operand);

  void TryRename(InstructionOperand* op);
  int GetRename(int virtual_register);
  template <typename... Args>
  void SetRename(Args...) {
    UNREACHABLE(/*REMOVE*/);
  }
  void SetRename(node_t node, node_t rename);
  void UpdateRenames(Instruction* instruction);
  void UpdateRenamesInPhi(PhiInstruction* phi);

  // Inform the instruction selection that {node} was just defined.
  void MarkAsDefined(node_t node);
  template <typename T>
  void MarkAsDefined(T* node) {
    UNREACHABLE(/*REMOVE*/);
  }

  // Inform the instruction selection that {node} has at least one use and we
  // will need to generate code for it.
  void MarkAsUsed(node_t node);
  template <typename T>
  void MarkAsUsed(T*) {
    UNREACHABLE(/*REMOVE*/);
  }

  // Sets the effect level of {node}.
  void SetEffectLevel(node_t node, int effect_level);
  template <typename T>
  void SetEffectLevel(T*, int) {
    UNREACHABLE(/*REMOVE*/);
  }

  // Inform the register allocation of the representation of the value produced
  // by {node}.
  template <typename... Args>
  void MarkAsRepresentation(Args...) {
    UNREACHABLE(/*REMOVE*/);
  }
  void MarkAsRepresentation(MachineRepresentation rep, node_t node);
  template <typename T>
  void MarkAsWord32(T*) {
    UNREACHABLE(/*REMOVE*/);
  }
  void MarkAsWord32(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kWord32, node);
  }
  template <typename T>
  void MarkAsWord64(T*) {
    UNREACHABLE(/*REMOVE*/);
  }
  void MarkAsWord64(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kWord64, node);
  }
  template <typename T>
  void MarkAsFloat32(T*) {
    UNREACHABLE(/*REMOVE*/);
  }
  void MarkAsFloat32(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kFloat32, node);
  }
  template <typename T>
  void MarkAsFloat64(T*) {
    UNREACHABLE(/*REMOVE*/);
  }
  void MarkAsFloat64(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kFloat64, node);
  }
  template <typename T>
  void MarkAsSimd128(T*) {
    UNREACHABLE(/*REMOVE*/);
  }
  void MarkAsSimd128(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kSimd128, node);
  }
  template <typename T>
  void MarkAsSimd256(T*) {
    UNREACHABLE(/*REMOVE*/);
  }

  void MarkAsSimd256(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kSimd256, node);
  }
  template <typename T>
  void MarkAsTagged(T*) {
    UNREACHABLE(/*REMOVE*/);
  }
  void MarkAsTagged(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kTagged, node);
  }
  template <typename T>
  void MarkAsCompressed(T*) {
    UNREACHABLE(/*REMOVE*/);
  }
  void MarkAsCompressed(node_t node) {
    MarkAsRepresentation(MachineRepresentation::kCompressed, node);
  }

  // Inform the register allocation of the representation of the unallocated
  // operand {op}.
  void MarkAsRepresentation(MachineRepresentation rep,
                            const InstructionOperand& op);

  enum CallBufferFlag {
    kCallCodeImmediate = 1u << 0,
    kCallAddressImmediate = 1u << 1,
    kCallTail = 1u << 2,
    kCallFixedTargetRegister = 1u << 3
  };
  using CallBufferFlags = base::Flags<CallBufferFlag>;

  // Initialize the call buffer with the InstructionOperands, nodes, etc,
  // corresponding
  // to the inputs and outputs of the call.
  // {call_code_immediate} to generate immediate operands to calls of code.
  // {call_address_immediate} to generate immediate operands to address calls.
  void InitializeCallBuffer(node_t call, CallBuffer* buffer,
                            CallBufferFlags flags, int stack_slot_delta = 0);
  bool IsTailCallAddressImmediate();

  void UpdateMaxPushedArgumentCount(size_t count);

  FrameStateDescriptor* GetFrameStateDescriptor(node_t node);
  size_t AddInputsToFrameStateDescriptor(FrameStateDescriptor* descriptor,
                                         node_t state, OperandGenerator* g,
                                         StateObjectDeduplicator* deduplicator,
                                         InstructionOperandVector* inputs,
                                         FrameStateInputKind kind, Zone* zone);
  size_t AddInputsToFrameStateDescriptor(StateValueList* values,
                                         InstructionOperandVector* inputs,
                                         OperandGenerator* g,
                                         StateObjectDeduplicator* deduplicator,
                                         node_t node, FrameStateInputKind kind,
                                         Zone* zone);
  size_t AddOperandToStateValueDescriptor(StateValueList* values,
                                          InstructionOperandVector* inputs,
                                          OperandGenerator* g,
                                          StateObjectDeduplicator* deduplicator,
                                          node_t input, MachineType type,
                                          FrameStateInputKind kind, Zone* zone);

  // ===========================================================================
  // ============= Architecture-specific graph covering methods. ===============
  // ===========================================================================

  // Visit nodes in the given block and generate code.
  void VisitBlock(block_t block);

  // Visit the node for the control flow at the end of the block, generating
  // code if necessary.
  void VisitControl(block_t block);

  // Visit the node and generate code, if any.
  void VisitNode(node_t node);

  // Visit the node and generate code for IEEE 754 functions.
  void VisitFloat64Ieee754Binop(Node*, InstructionCode code);
  void VisitFloat64Ieee754Unop(Node*, InstructionCode code);

#define DECLARE_GENERATOR_T(x) void Visit##x(node_t node);
  DECLARE_GENERATOR_T(Load)
  DECLARE_GENERATOR_T(StackPointerGreaterThan)
#undef DECLARE_GENERATOR_T

#define DECLARE_GENERATOR(x) void Visit##x(Node* node);
  // MACHINE_OP_LIST
  MACHINE_UNOP_32_LIST(DECLARE_GENERATOR)
  MACHINE_BINOP_32_LIST(DECLARE_GENERATOR)
  MACHINE_BINOP_64_LIST(DECLARE_GENERATOR)
  MACHINE_COMPARE_BINOP_LIST(DECLARE_GENERATOR)
  MACHINE_FLOAT32_BINOP_LIST(DECLARE_GENERATOR)
  MACHINE_FLOAT32_UNOP_LIST(DECLARE_GENERATOR)
  MACHINE_FLOAT64_BINOP_LIST(DECLARE_GENERATOR)
  MACHINE_FLOAT64_UNOP_LIST(DECLARE_GENERATOR)
  MACHINE_ATOMIC_OP_LIST(DECLARE_GENERATOR)
  DECLARE_GENERATOR(AbortCSADcheck)
  DECLARE_GENERATOR(DebugBreak)
  DECLARE_GENERATOR(Comment)
  DECLARE_GENERATOR(LoadImmutable)
  DECLARE_GENERATOR(Store)
  DECLARE_GENERATOR(StorePair)
  DECLARE_GENERATOR(StackSlot)
  DECLARE_GENERATOR(Word32Popcnt)
  DECLARE_GENERATOR(Word64Popcnt)
  DECLARE_GENERATOR(Word64Clz)
  DECLARE_GENERATOR(Word64Ctz)
  DECLARE_GENERATOR(Word64ClzLowerable)
  DECLARE_GENERATOR(Word64CtzLowerable)
  DECLARE_GENERATOR(Word64ReverseBits)
  DECLARE_GENERATOR(Word64ReverseBytes)
  DECLARE_GENERATOR(Simd128ReverseBytes)
  DECLARE_GENERATOR(Int64AbsWithOverflow)
  DECLARE_GENERATOR(BitcastTaggedToWord)
  DECLARE_GENERATOR(BitcastTaggedToWordForTagAndSmiBits)
  DECLARE_GENERATOR(BitcastWordToTagged)
  DECLARE_GENERATOR(BitcastWordToTaggedSigned)
  DECLARE_GENERATOR(TruncateFloat64ToWord32)
  DECLARE_GENERATOR(ChangeFloat32ToFloat64)
  DECLARE_GENERATOR(ChangeFloat64ToInt32)
  DECLARE_GENERATOR(ChangeFloat64ToInt64)
  DECLARE_GENERATOR(ChangeFloat64ToUint32)
  DECLARE_GENERATOR(ChangeFloat64ToUint64)
  DECLARE_GENERATOR(Float64SilenceNaN)
  DECLARE_GENERATOR(TruncateFloat64ToInt64)
  DECLARE_GENERATOR(TruncateFloat64ToUint32)
  DECLARE_GENERATOR(TruncateFloat32ToInt32)
  DECLARE_GENERATOR(TruncateFloat32ToUint32)
  DECLARE_GENERATOR(TryTruncateFloat32ToInt64)
  DECLARE_GENERATOR(TryTruncateFloat64ToInt64)
  DECLARE_GENERATOR(TryTruncateFloat32ToUint64)
  DECLARE_GENERATOR(TryTruncateFloat64ToUint64)
  DECLARE_GENERATOR(TryTruncateFloat64ToInt32)
  DECLARE_GENERATOR(TryTruncateFloat64ToUint32)
  DECLARE_GENERATOR(ChangeInt32ToFloat64)
  DECLARE_GENERATOR(BitcastWord32ToWord64)
  DECLARE_GENERATOR(ChangeInt32ToInt64)
  DECLARE_GENERATOR(ChangeInt64ToFloat64)
  DECLARE_GENERATOR(ChangeUint32ToFloat64)
  DECLARE_GENERATOR(ChangeUint32ToUint64)
  DECLARE_GENERATOR(TruncateFloat64ToFloat32)
  DECLARE_GENERATOR(TruncateInt64ToInt32)
  DECLARE_GENERATOR(RoundFloat64ToInt32)
  DECLARE_GENERATOR(RoundInt32ToFloat32)
  DECLARE_GENERATOR(RoundInt64ToFloat32)
  DECLARE_GENERATOR(RoundInt64ToFloat64)
  DECLARE_GENERATOR(RoundUint32ToFloat32)
  DECLARE_GENERATOR(RoundUint64ToFloat32)
  DECLARE_GENERATOR(RoundUint64ToFloat64)
  DECLARE_GENERATOR(BitcastFloat32ToInt32)
  DECLARE_GENERATOR(BitcastFloat64ToInt64)
  DECLARE_GENERATOR(BitcastInt32ToFloat32)
  DECLARE_GENERATOR(BitcastInt64ToFloat64)
  DECLARE_GENERATOR(Float64ExtractLowWord32)
  DECLARE_GENERATOR(Float64ExtractHighWord32)
  DECLARE_GENERATOR(Float64InsertLowWord32)
  DECLARE_GENERATOR(Float64InsertHighWord32)
  DECLARE_GENERATOR(Word32Select)
  DECLARE_GENERATOR(Word64Select)
  DECLARE_GENERATOR(Float32Select)
  DECLARE_GENERATOR(Float64Select)
  DECLARE_GENERATOR(LoadStackCheckOffset)
  DECLARE_GENERATOR(LoadFramePointer)
  DECLARE_GENERATOR(LoadParentFramePointer)
  DECLARE_GENERATOR(LoadRootRegister)
  DECLARE_GENERATOR(UnalignedLoad)
  DECLARE_GENERATOR(UnalignedStore)
  DECLARE_GENERATOR(Int32PairAdd)
  DECLARE_GENERATOR(Int32PairSub)
  DECLARE_GENERATOR(Int32PairMul)
  DECLARE_GENERATOR(Word32PairShl)
  DECLARE_GENERATOR(Word32PairShr)
  DECLARE_GENERATOR(Word32PairSar)
  DECLARE_GENERATOR(ProtectedLoad)
  DECLARE_GENERATOR(ProtectedStore)
  DECLARE_GENERATOR(LoadTrapOnNull)
  DECLARE_GENERATOR(StoreTrapOnNull)
  DECLARE_GENERATOR(MemoryBarrier)
  DECLARE_GENERATOR(SignExtendWord8ToInt32)
  DECLARE_GENERATOR(SignExtendWord16ToInt32)
  DECLARE_GENERATOR(SignExtendWord8ToInt64)
  DECLARE_GENERATOR(SignExtendWord16ToInt64)
  DECLARE_GENERATOR(SignExtendWord32ToInt64)
  DECLARE_GENERATOR(TraceInstruction)
  //
  MACHINE_SIMD128_OP_LIST(DECLARE_GENERATOR)
  MACHINE_SIMD256_OP_LIST(DECLARE_GENERATOR)
#undef DECLARE_GENERATOR

  // Visit the load node with a value and opcode to replace with.
  void VisitLoad(node_t node, node_t value, InstructionCode opcode);
  DECLARE_UNREACHABLE_TURBOSHAFT_FALLBACK(void, VisitLoad)
  void VisitLoadTransform(Node* node, Node* value, InstructionCode opcode);
  void VisitFinishRegion(Node* node);
  void VisitParameter(node_t node);
  void VisitIfException(Node* node);
  void VisitOsrValue(Node* node);
  void VisitPhi(Node* node);
  void VisitProjection(Node* node);
  void VisitConstant(node_t node);
  void VisitCall(node_t call, block_t handler = {});
  void VisitDeoptimizeIf(Node* node);
  void VisitDeoptimizeUnless(Node* node);
  void VisitDynamicCheckMapsWithDeoptUnless(Node* node);
  void VisitTrapIf(Node* node, TrapId trap_id);
  void VisitTrapUnless(Node* node, TrapId trap_id);
  void VisitTailCall(Node* call);
  void VisitGoto(block_t target);
  void VisitBranch(node_t input, block_t tbranch, block_t fbranch);
  void VisitSwitch(Node* node, const SwitchInfo& sw);
  void VisitDeoptimize(DeoptimizeReason reason, id_t node_id,
                       FeedbackSource const& feedback, node_t frame_state);
  void VisitSelect(Node* node);
  void VisitReturn(node_t node);
  void VisitThrow(Node* node);
  void VisitRetain(Node* node);
  void VisitUnreachable(Node* node);
  void VisitStaticAssert(Node* node);
  void VisitDeadValue(Node* node);

  void TryPrepareScheduleFirstProjection(node_t maybe_projection);

  void VisitStackPointerGreaterThan(node_t node, FlagsContinuation* cont);

  void VisitWordCompareZero(node_t user, node_t value, FlagsContinuation* cont);

  void EmitPrepareArguments(ZoneVector<PushParameter>* arguments,
                            const CallDescriptor* call_descriptor, node_t node);
  void EmitPrepareResults(ZoneVector<PushParameter>* results,
                          const CallDescriptor* call_descriptor, node_t node);

  // In LOONG64, calling convention uses free GP param register to pass
  // floating-point arguments when no FP param register is available. But
  // gap does not support moving from FPR to GPR, so we add EmitMoveFPRToParam
  // to complete movement.
  void EmitMoveFPRToParam(InstructionOperand* op, LinkageLocation location);
  // Moving floating-point param from GP param register to FPR to participate in
  // subsequent operations, whether CallCFunction or normal floating-point
  // operations.
  void EmitMoveParamToFPR(node_t node, int index);

  bool CanProduceSignalingNaN(Node* node);

  void AddOutputToSelectContinuation(OperandGenerator* g, int first_input_index,
                                     node_t node);

  // ===========================================================================
  // ============= Vector instruction (SIMD) helper fns. =======================
  // ===========================================================================

#if V8_ENABLE_WEBASSEMBLY
  // Canonicalize shuffles to make pattern matching simpler. Returns the shuffle
  // indices, and a boolean indicating if the shuffle is a swizzle (one input).
  void CanonicalizeShuffle(Node* node, uint8_t* shuffle, bool* is_swizzle);

  // Swaps the two first input operands of the node, to help match shuffles
  // to specific architectural instructions.
  void SwapShuffleInputs(Node* node);
#endif  // V8_ENABLE_WEBASSEMBLY

  // ===========================================================================

  schedule_t schedule() const { return schedule_; }
  Linkage* linkage() const { return linkage_; }
  InstructionSequence* sequence() const { return sequence_; }
  Zone* instruction_zone() const { return sequence()->zone(); }
  Zone* zone() const { return zone_; }

  void set_instruction_selection_failed() {
    instruction_selection_failed_ = true;
  }
  bool instruction_selection_failed() { return instruction_selection_failed_; }

  void MarkPairProjectionsAsWord32(Node* node);
  bool IsSourcePositionUsed(Node* node);
  void VisitWord32AtomicBinaryOperation(Node* node, ArchOpcode int8_op,
                                        ArchOpcode uint8_op,
                                        ArchOpcode int16_op,
                                        ArchOpcode uint16_op,
                                        ArchOpcode word32_op);
  void VisitWord64AtomicBinaryOperation(Node* node, ArchOpcode uint8_op,
                                        ArchOpcode uint16_op,
                                        ArchOpcode uint32_op,
                                        ArchOpcode uint64_op);
  void VisitWord64AtomicNarrowBinop(Node* node, ArchOpcode uint8_op,
                                    ArchOpcode uint16_op, ArchOpcode uint32_op);

#if V8_TARGET_ARCH_64_BIT
  bool ZeroExtendsWord32ToWord64(Node* node, int recursion_depth = 0);
  bool ZeroExtendsWord32ToWord64NoPhis(Node* node);

  enum Upper32BitsState : uint8_t {
    kNotYetChecked,
    kUpperBitsGuaranteedZero,
    kNoGuarantee,
  };
#endif  // V8_TARGET_ARCH_64_BIT

  struct FrameStateInput {
    FrameStateInput(node_t node_, FrameStateInputKind kind_)
        : node(node_), kind(kind_) {}

    node_t node;
    FrameStateInputKind kind;

    struct Hash {
      size_t operator()(FrameStateInput const& source) const {
        return base::hash_combine(source.node,
                                  static_cast<size_t>(source.kind));
      }
    };

    struct Equal {
      bool operator()(FrameStateInput const& lhs,
                      FrameStateInput const& rhs) const {
        return lhs.node == rhs.node && lhs.kind == rhs.kind;
      }
    };
  };

  struct CachedStateValues;
  class CachedStateValuesBuilder;

  // ===========================================================================

  Zone* const zone_;
  Linkage* const linkage_;
  InstructionSequence* const sequence_;
  SourcePositionTable* const source_positions_;
  SourcePositionMode const source_position_mode_;
  Features features_;
  schedule_t const schedule_;
  block_t current_block_;
  ZoneVector<Instruction*> instructions_;
  InstructionOperandVector continuation_inputs_;
  InstructionOperandVector continuation_outputs_;
  InstructionOperandVector continuation_temps_;
  BitVector defined_;
  BitVector used_;
  IntVector effect_level_;
  int current_effect_level_;
  IntVector virtual_registers_;
  IntVector virtual_register_rename_;
  InstructionScheduler* scheduler_;
  EnableScheduling enable_scheduling_;
  EnableRootsRelativeAddressing enable_roots_relative_addressing_;
  EnableSwitchJumpTable enable_switch_jump_table_;
  ZoneUnorderedMap<FrameStateInput, CachedStateValues*,
                   typename FrameStateInput::Hash,
                   typename FrameStateInput::Equal>
      state_values_cache_;

  Frame* frame_;
  bool instruction_selection_failed_;
  ZoneVector<std::pair<int, int>> instr_origins_;
  EnableTraceTurboJson trace_turbo_;
  TickCounter* const tick_counter_;
  // The broker is only used for unparking the LocalHeap for diagnostic printing
  // for failed StaticAsserts.
  JSHeapBroker* const broker_;

  // Store the maximal unoptimized frame height and an maximal number of pushed
  // arguments (for calls). Later used to apply an offset to stack checks.
  size_t* max_unoptimized_frame_height_;
  size_t* max_pushed_argument_count_;

#if V8_TARGET_ARCH_64_BIT
  // Holds lazily-computed results for whether phi nodes guarantee their upper
  // 32 bits to be zero. Indexed by node ID; nobody reads or writes the values
  // for non-phi nodes.
  ZoneVector<Upper32BitsState> phi_states_;
#endif
};

using InstructionSelector = InstructionSelectorT<TurbofanAdapter>;

// extern template class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
//     InstructionSelectorT<TurbofanAdapter>;
// extern template class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
//     InstructionSelectorT<TurboshaftAdapter>;

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_H_
