// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <initializer_list>
#include <type_traits>

#include "src/base/logging.h"
#include "src/base/small-vector.h"
#include "src/base/template-utils.h"
#include "src/base/vector.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/tick-counter.h"
#include "src/codegen/turbo-assembler.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/linkage.h"
#include "src/compiler/turboshaft/cfg.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/select-instructions.h"

// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD
// This is the platform-independent part of instruction selection and only
// included by the platform-specific version. Therefore it contains definitions,
// unnamed namespaces and no include guards.

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

namespace {  // NOLINT(build/namespaces)

template <class T>
struct default_value {
  static constexpr T value = T();
};

struct default_virtual_register {
  static constexpr int value = InstructionOperand::kInvalidVirtualRegister;
};
constexpr int default_virtual_register::value;

template <class Key, class Value, class Default = default_value<Value>>
class Sidetable {
 public:
  Value& operator[](Key key) {
    size_t index = key.id();
    DCHECK_LT(index, table_.size());
    return table_[index];
  }
  explicit Sidetable(Zone* zone, size_t initial_size)
      : table_(initial_size, Default::value, zone) {}

 private:
  ZoneVector<Value> table_;
};

template <class Key, class Value, class Default = default_value<Value>>
class ResizingSidetable {
 public:
  Value& operator[](Key key) {
    size_t index = key;
    if (index >= table_.size()) {
      table_.resize(index + 100 + index / 2, Default::value);
    }
    return table_[index];
  }
  explicit ResizingSidetable(Zone* zone) : table_(zone) {}

 private:
  ZoneVector<Value> table_;
};

enum class Binop { kBitwiseAnd, kAdd, kSub };

bool IsCommutative(Binop binop) {
  switch (binop) {
    case Binop::kBitwiseAnd:
      return true;
    case Binop::kAdd:
      return true;
    case Binop::kSub:
      return false;
  }
}

class FlagsContinuation final {
 public:
  enum class Kind : uint8_t {
    kBranch,
    kSet  //, kDeoptimize, kTrap, kSelect
  };
  Kind kind() const { return kind_; }

  static FlagsContinuation ForBranch(const turboshaft::Block& true_block,
                                     const turboshaft::Block& false_block) {
    FlagsContinuation cont(Kind::kBranch);
    cont.union_.branch_ = {&true_block, &false_block};
    return cont;
  }
  const Block& true_block() const {
    DCHECK_EQ(kind_, Kind::kBranch);
    return *union_.branch_.true_block_;
  }
  const Block& false_block() const {
    DCHECK_EQ(kind_, Kind::kBranch);
    return *union_.branch_.true_block_;
  }

  static FlagsContinuation ForSet(OpIndex result) {
    FlagsContinuation cont(Kind::kSet);
    cont.union_.set_ = {result};
    return cont;
  }
  OpIndex result() const {
    DCHECK_EQ(kind_, Kind::kSet);
    return union_.set_.result_;
  }

 private:
  explicit FlagsContinuation(Kind kind) : kind_(kind) {}

  const Kind kind_;
  union Union {
    struct {
      const Block* true_block_;
      const Block* false_block_;
    } branch_;
    struct {
      OpIndex result_;
      DeoptimizeKind deopt_kind_;
      DeoptimizeReason reason_;
      NodeId node_id_;
      FeedbackSource feedback_;
      void* frame_state_;  // TODO(tebbi): What to choose here?
      InstructionOperand* extra_args_;
      int extra_args_count_;
    } deoptimize_;
    struct {
      OpIndex result_;
    } set_;
    struct {
      TrapId trap_id_;
    } trap_;
    struct {
      OpIndex true_value_;
      OpIndex false_value_;
    } select_;

    Union() : branch_{} {}
  } union_;
};

Constant ToConstant(const ConstantOp& op) {
  switch (op.kind) {
    using Kind = ConstantOp::Kind;
    case Kind::kWord32:
      return Constant(static_cast<int32_t>(op.word32()));
    case Kind::kWord64:
      return Constant(static_cast<int64_t>(op.word64()));
    case Kind::kExternal:
      return Constant(op.external_reference());
    case Kind::kHeapObject:
    case Kind::kCompressedHeapObject:
      return Constant(op.handle(), op.kind == Kind::kCompressedHeapObject);
    case Kind::kNumber:
      return Constant(op.number());
    case Kind::kFloat32:
      return Constant(op.float32());
    case Kind::kFloat64:
      return Constant(op.float64());
    case Kind::kTaggedIndex:
    case Kind::kDelayedString:
      UNIMPLEMENTED();
  }
}

constexpr InstructionCode EncodeCallDescriptorFlags(
    InstructionCode opcode, CallDescriptor::Flags flags) {
  // Note: Not all bits of `flags` are preserved.
  STATIC_ASSERT(CallDescriptor::kFlagsBitsEncodedInInstructionCode ==
                MiscField::kSize);
  DCHECK(Instruction::IsCallWithDescriptorFlags(opcode));
  // TODO(tebbi): support frame states
  return opcode | MiscField::encode(flags & MiscField::kMax &
                                    ~CallDescriptor::kNeedsFrameState);
}

struct PlatformSpecificInstructionSelector;

struct InstructionSelector {
  Zone* const temp_zone;
  const Graph& graph;
  InstructionSequence* const sequence;
  Frame* const frame;
  Linkage* linkage;
  base::Flags<CpuFeature> cpu_features;
  TickCounter* const tick_counter;
  const bool enable_instruction_scheduling;
  const bool enable_roots_relative_addressing;
  size_t* max_pushed_argument_count;

  Zone* const graph_zone = graph.graph_zone();
  InstructionScheduler* scheduler = nullptr;
  bool instruction_selection_failed = false;

  const Block* current_block = nullptr;
  int current_effect_level = 0;
  ZoneVector<Instruction*> instruction_buffer{temp_zone};

  Sidetable<OpIndex, uint32_t> graph_use_count{temp_zone, graph.op_id_count()};
  Sidetable<OpIndex, uint32_t> used_by_instruction{temp_zone,
                                                   graph.op_id_count()};
  Sidetable<OpIndex, uint32_t> operation_has_been_processed{
      temp_zone, graph.op_id_count()};
  Sidetable<OpIndex, int> effect_level{temp_zone, graph.op_id_count()};
  ResizingSidetable<int, int, default_virtual_register> virtual_register_rename{
      temp_zone};
  Sidetable<OpIndex, int, default_virtual_register> virtual_registers{
      temp_zone, graph.op_id_count()};

  InstructionSelector(Zone* temp_zone, const Graph& graph,
                      InstructionSequence* sequence, Frame* frame,
                      Linkage* linkage, base::Flags<CpuFeature> cpu_features,
                      TickCounter* tick_counter,
                      bool enable_instruction_scheduling,
                      bool enable_roots_relative_addressing,
                      size_t* max_pushed_argument_count)
      : temp_zone(temp_zone),
        graph(graph),
        sequence(sequence),
        frame(frame),
        linkage(linkage),
        cpu_features(cpu_features),
        tick_counter(tick_counter),
        enable_instruction_scheduling(enable_instruction_scheduling),
        enable_roots_relative_addressing(enable_roots_relative_addressing),
        max_pushed_argument_count(max_pushed_argument_count) {}

  bool Run();

  template <class T = PlatformSpecificInstructionSelector>
  PlatformSpecificInstructionSelector& specific() {
    return *static_cast<T*>(this);
  }
  template <class T = PlatformSpecificInstructionSelector>
  const PlatformSpecificInstructionSelector& specific() const {
    return *static_cast<const T*>(this);
  }

  Isolate* isolate() { return sequence->isolate(); }

  void Visit(const Block& block);
  void Visit(const Operation& op);
  template <class Op>
  void Visit(const Op& op);

  Instruction* Emit(Instruction* instr) {
    instruction_buffer.push_back(instr);
    return instr;
  }

  Instruction* Emit(InstructionCode opcode,
                    std::initializer_list<InstructionOperand> outputs,
                    std::initializer_list<InstructionOperand> inputs,
                    std::initializer_list<InstructionOperand> temps = {}) {
    return Emit(opcode, base::VectorOf(outputs), base::VectorOf(inputs),
                base::VectorOf(temps));
  }

  Instruction* Emit(InstructionCode opcode,
                    base::Vector<const InstructionOperand> outputs,
                    base::Vector<const InstructionOperand> inputs,
                    base::Vector<const InstructionOperand> temps =
                        base::VectorOf<const InstructionOperand>({})) {
    if (outputs.size() >= Instruction::kMaxOutputCount ||
        inputs.size() >= Instruction::kMaxInputCount ||
        temps.size() >= Instruction::kMaxTempCount) {
      instruction_selection_failed = true;
      return nullptr;
    }
    return Emit(Instruction::New(sequence->zone(), opcode, outputs.size(),
                                 outputs.data(), inputs.size(), inputs.data(),
                                 temps.size(), temps.data()));
  }

  Instruction* EmitWithContinuation(
      InstructionCode opcode, const FlagsContinuation& cont,
      std::initializer_list<InstructionOperand> outputs,
      std::initializer_list<InstructionOperand> inputs,
      std::initializer_list<InstructionOperand> temps = {}) {
    return EmitWithContinuation(opcode, cont, base::VectorOf(outputs),
                                base::VectorOf(inputs), base::VectorOf(temps));
  }
  Instruction* EmitWithContinuation(
      InstructionCode opcode, const FlagsContinuation& cont,
      base::Vector<const InstructionOperand> outputs,
      base::Vector<const InstructionOperand> inputs,
      base::Vector<const InstructionOperand> temps = {});

  const Operation& Get(OpIndex op_idx) { return graph.Get(op_idx); }
  OpIndex Index(const Operation& op) { return graph.Index(op); }
  static RpoNumber Index(const Block& block) {
    return RpoNumber::FromInt(ToUnderlyingType(block.index));
  }

  void MarkAsUsed(OpIndex op_idx) { used_by_instruction[op_idx] = true; }
  bool IsUsed(const Operation& op) {
    if (op.properties().is_required_when_unused) return true;
    return used_by_instruction[Index(op)];
  }
  void MarkAsProcessed(OpIndex op_idx) {
    operation_has_been_processed[op_idx] = true;
  }
  bool HasBeenProcessed(OpIndex op_idx) {
    return operation_has_been_processed[op_idx];
  }
  bool IsLive(OpIndex op_idx) {
    return !HasBeenProcessed(op_idx) && IsUsed(Get(op_idx));
  }

  bool CanCover(OpIndex input) {
    // 1. Both {user} and {input} must be in the same basic block.
    if (!current_block->Contains(input)) return false;

    // 2. {input} must be owned by the {user}.
    uint32_t use_count = graph_use_count[input];
    DCHECK_GT(use_count, 0);
    if (use_count != 1) return false;

    // 3. Impure {input}s must match the effect level of the user.
    if (effect_level[input] != current_effect_level &&
        !Get(input).properties().is_pure) {
      return false;
    }

    return true;
  }

  void MarkAsRepresentation(MachineRepresentation rep, OpIndex op_idx) {
    sequence->MarkAsRepresentation(rep, GetVirtualRegister(op_idx));
  }

  int GetVirtualRegister(OpIndex op) {
    int& vreg = virtual_registers[op];
    if (vreg == InstructionOperand::kInvalidVirtualRegister) {
      vreg = sequence->NextVirtualRegister();
    }
    DCHECK_LT(vreg, sequence->VirtualRegisterCount());
    return vreg;
  }
  void SetRename(OpIndex op, OpIndex rename) {
    virtual_register_rename[GetVirtualRegister(op)] =
        GetVirtualRegister(rename);
  }
  void ApplyRenamings(Instruction* instruction) {
    for (size_t i = 0; i < instruction->InputCount(); i++) {
      InstructionOperand* operand = instruction->InputAt(i);
      if (!operand->IsUnallocated()) continue;
      UnallocatedOperand* unalloc = UnallocatedOperand::cast(operand);
      int vreg = unalloc->virtual_register();
      int rename = virtual_register_rename[vreg];
      if (rename != InstructionOperand::kInvalidVirtualRegister) {
        *unalloc = UnallocatedOperand(*unalloc, rename);
      }
    }
  }
  void ApplyRenamings(PhiInstruction* phi) {
    for (size_t i = 0; i < phi->operands().size(); i++) {
      int renamed = virtual_register_rename[phi->operands()[i]];
      if (renamed != InstructionOperand::kInvalidVirtualRegister) {
        phi->RenameInput(i, renamed);
      }
    }
  }

  void StartBlock(RpoNumber rpo) {
    if (scheduler) {
      scheduler->StartBlock(rpo);
    } else {
      sequence->StartBlock(rpo);
    }
  }
  void EndBlock(RpoNumber rpo) {
    if (scheduler) {
      scheduler->EndBlock(rpo);
    } else {
      sequence->EndBlock(rpo);
    }
  }
  void AddInstruction(Instruction* instr) {
    if (scheduler) {
      scheduler->AddInstruction(instr);
    } else {
      sequence->AddInstruction(instr);
    }
  }
  void AddTerminator(Instruction* instr) {
    if (scheduler) {
      scheduler->AddTerminator(instr);
    } else {
      sequence->AddInstruction(instr);
    }
  }

  UnallocatedOperand Define(OpIndex op, UnallocatedOperand operand) {
    DCHECK_EQ(operand.virtual_register(), GetVirtualRegister(op));
    MarkAsProcessed(op);
    return operand;
  }
  InstructionOperand DefineAsConstant(const ConstantOp& op) {
    OpIndex idx = Index(op);
    MarkAsProcessed(idx);
    int virtual_register = GetVirtualRegister(idx);
    sequence->AddConstant(virtual_register, ToConstant(op));
    return ConstantOperand(virtual_register);
  }
  InstructionOperand DefineSameAsInput(OpIndex op, int input_index) {
    return Define(op, UnallocatedOperand(GetVirtualRegister(op), input_index));
  }
  InstructionOperand DefineAsLocation(OpIndex op, LinkageLocation location) {
    return Define(op, ToUnallocatedOperand(location, GetVirtualRegister(op)));
  }
  InstructionOperand DefineAsDualLocation(OpIndex op,
                                          LinkageLocation primary_location,
                                          LinkageLocation secondary_location) {
    return Define(
        op, ToDualLocationUnallocatedOperand(
                primary_location, secondary_location, GetVirtualRegister(op)));
  }
  InstructionOperand DefineAsRegister(OpIndex op) {
    return Define(op, UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER,
                                         GetVirtualRegister(op)));
  }

  UnallocatedOperand Use(OpIndex value, UnallocatedOperand operand) {
    DCHECK_EQ(operand.virtual_register(), GetVirtualRegister(value));
    MarkAsUsed(value);
    return operand;
  }
  InstructionOperand Use(OpIndex value) {
    return Use(value, UnallocatedOperand(UnallocatedOperand::NONE,
                                         UnallocatedOperand::USED_AT_START,
                                         GetVirtualRegister(value)));
  }
  InstructionOperand UseRegisterOrSlot(OpIndex value) {
    return Use(value, UnallocatedOperand(UnallocatedOperand::REGISTER_OR_SLOT,
                                         UnallocatedOperand::USED_AT_START,
                                         GetVirtualRegister(value)));
  }
  InstructionOperand UseRegister(OpIndex value) {
    return Use(value, UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER,
                                         UnallocatedOperand::USED_AT_START,
                                         GetVirtualRegister(value)));
  }
  InstructionOperand UseFixed(OpIndex value, Register reg) {
    return Use(value,
               UnallocatedOperand(UnallocatedOperand::FIXED_REGISTER,
                                  reg.code(), GetVirtualRegister(value)));
  }
  // Use a unique register for the node that does not alias any temporary or
  // output registers.
  InstructionOperand UseUniqueRegister(OpIndex value) {
    return Use(value, UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER,
                                         UnallocatedOperand::USED_AT_END,
                                         GetVirtualRegister(value)));
  }
  enum class RegisterUseKind { kUseRegister, kUseUniqueRegister };
  InstructionOperand UseRegister(OpIndex value, RegisterUseKind unique_reg) {
    if (V8_LIKELY(unique_reg == RegisterUseKind::kUseRegister)) {
      return UseRegister(value);
    } else {
      DCHECK_EQ(unique_reg, RegisterUseKind::kUseUniqueRegister);
      return UseUniqueRegister(value);
    }
  }
  InstructionOperand UseImmediate(int32_t immediate) {
    return sequence->AddImmediate(Constant(immediate));
  }
  InstructionOperand UseImmediate(const ConstantOp& value) {
    return sequence->AddImmediate(ToConstant(value));
  }
  InstructionOperand UseLabel(const Block& block) {
    return sequence->AddImmediate(Constant(Index(block)));
  }
  InstructionOperand UseLocation(OpIndex value, LinkageLocation location) {
    return Use(value,
               ToUnallocatedOperand(location, GetVirtualRegister(value)));
  }

  bool CanUseRootsRegister() {
    return linkage->GetIncomingDescriptor()->flags() &
           CallDescriptor::kCanUseRoots;
  }
  bool CanAddressRelativeToRootsRegister(const ExternalReference& reference) {
    // There are three things to consider here:
    // 1. CanUseRootsRegister: Is kRootRegister initialized?
    const bool root_register_is_available_and_initialized =
        CanUseRootsRegister();
    if (!root_register_is_available_and_initialized) return false;

    // 2. enable_roots_relative_addressing_: Can we address everything on the
    // heap
    //    through the root register, i.e. are root-relative addresses to
    //    arbitrary addresses guaranteed not to change between code generation
    //    and execution?
    if (enable_roots_relative_addressing) return true;

    // 3. IsAddressableThroughRootRegister: Is the target address guaranteed to
    //    have a fixed root-relative offset? If so, we can ignore 2.
    const bool this_root_relative_offset_is_constant =
        TurboAssemblerBase::IsAddressableThroughRootRegister(isolate(),
                                                             reference);
    return this_root_relative_offset_is_constant;
  }

  void UpdateMaxPushedArgumentCount(size_t count) {
    *max_pushed_argument_count = std::max(count, *max_pushed_argument_count);
  }

  // Platform-specific operations not defined in this file.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-internal"
  bool CanBeImmediate(const Operation& value);
  void VisitWordNotEqualZero(const Operation& value,
                             const FlagsContinuation& cont);
  void VisitBinop(OpIndex op, Binop op_kind, MachineRepresentation rep,
                  OpIndex left, OpIndex right);
  void VisitStackPointerGreaterThan(const StackPointerGreaterThanOp& op,
                                    const FlagsContinuation& cont);
  void EmitPrepareArguments(base::Vector<const OpIndex> arguments,
                            const CallDescriptor* call_descriptor);
  void EmitPrepareResults(base::Vector<const OpIndex> results,
                          const CallDescriptor* call_descriptor);
#pragma clang diagnostic pop
};

bool InstructionSelector::Run() {
  instruction_buffer.reserve(graph.op_id_count());

  for (const Operation& op : graph.AllOperations()) {
    for (OpIndex input : op.inputs()) {
      ++graph_use_count[input];
    }
  }

  // Mark the loop phi backedges as used.
  for (const Block& block : graph.blocks()) {
    if (!block.IsLoop()) continue;
    DCHECK_EQ(2, block.predecessors.size());
    for (const Operation& instr : graph.operations(block)) {
      if (auto* phi = instr.TryCast<PhiOp>()) {
        MarkAsUsed(phi->inputs()[1]);
      }
    }
  }

  // Visit each basic block in post order.
  // The emitted instructions are put into {instruction_buffer}.
  for (const Block& block : base::Reversed(graph.blocks())) {
    Visit(block);
    if (instruction_selection_failed) return false;
  }

  if (enable_instruction_scheduling) {
    scheduler = temp_zone->New<InstructionScheduler>(temp_zone, sequence);
  }

  // Move the emitted instructions from {instruction_buffer} into the actual
  // InstructionSequence.
  for (const Block& block : graph.blocks()) {
    RpoNumber rpo_number = Index(block);
    InstructionBlock* instruction_block =
        sequence->InstructionBlockAt(rpo_number);
    for (size_t i = 0; i < instruction_block->phis().size(); i++) {
      ApplyRenamings(instruction_block->PhiAt(i));
    }
    size_t end = instruction_block->code_end();
    size_t start = instruction_block->code_start();
    DCHECK_LE(end, start);
    StartBlock(rpo_number);
    if (end != start) {
      while (start-- > end + 1) {
        ApplyRenamings(instruction_buffer[start]);
        AddInstruction(instruction_buffer[start]);
      }
      ApplyRenamings(instruction_buffer[end]);
      AddTerminator(instruction_buffer[end]);
    }
    EndBlock(rpo_number);
  }
#if DEBUG
  sequence->ValidateSSA();
#endif
  return true;
}

void InstructionSelector::Visit(const Block& block) {
  DCHECK(!current_block);
  current_block = &block;
  auto current_num_instructions = [&] {
    DCHECK_GE(kMaxInt, instruction_buffer.size());
    return static_cast<int>(instruction_buffer.size());
  };
  int current_block_end = current_num_instructions();

  int current_effect_level = 0;
  for (const Operation& instr : graph.operations(block)) {
    effect_level[Index(instr)] = current_effect_level;
    if (instr.properties().can_write) {
      ++current_effect_level;
    }
  }

  auto FinishEmittedInstructions = [&](const Operation& op,
                                       int instruction_start) {
    if (instruction_selection_failed) return false;
    if (current_num_instructions() == instruction_start) return true;
    std::reverse(instruction_buffer.begin() + instruction_start,
                 instruction_buffer.end());
    // TODO(tebbi): support source positions
    return true;
  };

  // Visit code in reverse control flow order, because architecture-specific
  // matching may cover more than one node at a time.
  for (const Operation& op : base::Reversed(graph.operations(block))) {
    int current_instr_end = current_num_instructions();
    // Skip nodes that are unused or already defined.
    if (IsUsed(op) && !HasBeenProcessed(Index(op))) {
      // Generate code for this node "top down", but schedule the code "bottom
      // up".
      Visit(op);
      if (!FinishEmittedInstructions(op, current_instr_end)) return;
    }
  }

  // We're done with the block.
  RpoNumber rpo_number = RpoNumber::FromInt(ToUnderlyingType(block.index));
  InstructionBlock* instruction_block =
      sequence->InstructionBlockAt(rpo_number);
  if (current_num_instructions() == current_block_end) {
    // Avoid empty block: insert a {kArchNop} instruction.
    Emit(Instruction::New(sequence->zone(), kArchNop));
  }
  instruction_block->set_code_start(current_num_instructions());
  instruction_block->set_code_end(current_block_end);
  current_block = nullptr;
}

template <class Op>
void InstructionSelector::Visit(const Op& op) {
  STATIC_ASSERT((std::is_base_of<Operation, Op>::value));
  STATIC_ASSERT(!(std::is_same<Operation, Op>::value));
  FATAL("Unexpected operation #%d: %s", Index(op).id(), op.ToString().c_str());
}

template <>
void InstructionSelector::Visit(const ConstantOp& op) {
  MarkAsRepresentation(op.Representation(), Index(op));

  // We must emit a NOP here because every live range needs a defining
  // instruction in the register allocator.
  Emit(kArchNop, {DefineAsConstant(op)}, {});
}
template <>
void InstructionSelector::Visit(const PhiOp& op) {
  MarkAsRepresentation(op.rep, Index(op));

  const size_t input_count = op.inputs().size();
  DCHECK_EQ(input_count, current_block->predecessors.size());
  PhiInstruction* phi = sequence->zone()->New<PhiInstruction>(
      sequence->zone(), GetVirtualRegister(Index(op)),
      static_cast<size_t>(input_count));
  sequence->InstructionBlockAt(Index(*current_block))->AddPhi(phi);
  for (size_t i = 0; i < input_count; ++i) {
    OpIndex input = op.inputs()[i];
    MarkAsUsed(input);
    phi->SetInput(i, GetVirtualRegister(input));
  }
}
template <>
void InstructionSelector::Visit(const BranchOp& op) {
  DCHECK_EQ(current_block->successors.size(), 2);
  const Block& tbranch = *current_block->successors[0];
  const Block& fbranch = *current_block->successors[1];

  FlagsContinuation cont = FlagsContinuation::ForBranch(tbranch, fbranch);
  VisitWordNotEqualZero(Get(op.condition()), cont);
}
template <>
void InstructionSelector::Visit(const BitwiseAndOp& op) {
  VisitBinop(Index(op), Binop::kBitwiseAnd, op.rep, op.left(), op.right());
}

template <>
void InstructionSelector::Visit(const ParameterOp& op) {
  int index = op.parameter_index;
  MachineRepresentation rep = linkage->GetParameterType(index).representation();
  MarkAsRepresentation(rep, Index(op));
  InstructionOperand operand =
      linkage->ParameterHasSecondaryLocation(index)
          ? DefineAsDualLocation(Index(op),
                                 linkage->GetParameterLocation(index),
                                 linkage->GetParameterSecondaryLocation(index))
          : DefineAsLocation(Index(op), linkage->GetParameterLocation(index));
  Emit(kArchNop, {operand}, {});
}

template <>
void InstructionSelector::Visit(const ReturnOp& op) {
  base::SmallVector<InstructionOperand, 4> inputs =
      base::make_array(UseImmediate(op.pop_count));
  auto return_values = op.return_values();
  for (size_t i = 0; i < return_values.size(); ++i) {
    inputs.push_back(
        UseLocation(return_values[i], linkage->GetReturnLocation(i)));
  }
  Emit(kArchRet, {}, base::VectorOf(inputs));
}

template <>
void InstructionSelector::Visit(const GotoOp& op) {
  Emit(kArchJmp, {}, {UseLabel(*op.destination)});
}

template <>
void InstructionSelector::Visit(const CheckLazyDeoptOp& op) {
  // TODO(tebbi): implement lazy deopts.
}

template <>
void InstructionSelector::Visit(const CallOp& op) {
  auto call_descriptor = op.descriptor;
  SaveFPRegsMode mode = call_descriptor->NeedsCallerSavedFPRegisters()
                            ? SaveFPRegsMode::kSave
                            : SaveFPRegsMode::kIgnore;

  if (call_descriptor->NeedsCallerSavedRegisters()) {
    Emit(kArchSaveCallerRegisters | MiscField::encode(static_cast<int>(mode)),
         {}, {});
  }

  // FrameStateDescriptor* frame_state_descriptor = nullptr;
  // if (call_descriptor->NeedsFrameState()) {
  //   frame_state_descriptor = GetFrameStateDescriptor(FrameState{
  //       node->InputAt(static_cast<int>(call_descriptor->InputCount()))});
  // }

  // CallBuffer buffer(temp_zone, call_descriptor, frame_state_descriptor);
  CallDescriptor::Flags flags = call_descriptor->flags();

  // Compute InstructionOperands for inputs and outputs.
  // TODO(turbofan): on some architectures it's probably better to use
  // the code object in a register if there are multiple uses of it.
  // Improve constant pool and the heuristics in the register allocator
  // for where to emit constants.
  // CallBufferFlags call_buffer_flags(kCallCodeImmediate |
  // kCallAddressImmediate);

  base::SmallVector<InstructionOperand, 4> outputs;
  base::SmallVector<InstructionOperand, 16> inputs;

  size_t ret_count = call_descriptor->ReturnCount();
  base::SmallVector<OpIndex, 4> results;
  if (ret_count == 1) {
    results = base::make_array(Index(op));
  } else if (ret_count > 1) {
    // TODO(tebbi): Collect projections here.
    UNIMPLEMENTED();
  }
  for (size_t i = 0; i < ret_count; ++i) {
    // TODO(tebbi): Skip if output is unused.
    LinkageLocation location = call_descriptor->GetReturnLocation(i);
    MachineRepresentation rep = location.GetType().representation();
    InstructionOperand output = DefineAsLocation(results[i], location);
    MarkAsRepresentation(rep, results[i]);
    if (!UnallocatedOperand::cast(output).HasFixedSlotPolicy()) {
      outputs.push_back(output);
    }
  }

  OpIndex callee_idx = op.callee();
  const Operation& callee = Get(callee_idx);
  bool call_address_immediate = true;
  bool fixed_target_register =
      call_descriptor->flags() & CallDescriptor::kFixedTargetRegister;
  switch (call_descriptor->kind()) {
    case CallDescriptor::kCallCodeObject:
      inputs.push_back(
          callee.Is<ConstantOp>()
              ? UseImmediate(callee.Cast<ConstantOp>())
              : fixed_target_register
                    ? UseFixed(callee_idx, kJavaScriptCallCodeStartRegister)
                    : UseRegister(callee_idx));
      break;
    case CallDescriptor::kCallAddress:
      inputs.push_back(
          (call_address_immediate && callee.Is<ConstantOp>())
              ? UseImmediate(callee.Cast<ConstantOp>())
              : fixed_target_register
                    ? UseFixed(callee_idx, kJavaScriptCallCodeStartRegister)
                    : UseRegister(callee_idx));
      break;
#if V8_ENABLE_WEBASSEMBLY
    case CallDescriptor::kCallWasmCapiFunction:
    case CallDescriptor::kCallWasmFunction:
    case CallDescriptor::kCallWasmImportWrapper:
      inputs.push_back(
          (call_address_immediate && callee.Is<ConstantOp>())
              ? UseImmediate(callee.Cast<ConstantOp>())
              : fixed_target_register
                    ? UseFixed(callee_idx, kJavaScriptCallCodeStartRegister)
                    : UseRegister(callee_idx));
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    case CallDescriptor::kCallBuiltinPointer:
      // The common case for builtin pointers is to have the target in a
      // register. If we have a constant, we use a register anyway to simplify
      // related code.
      inputs.push_back(
          fixed_target_register
              ? UseFixed(callee_idx, kJavaScriptCallCodeStartRegister)
              : UseRegister(callee_idx));
      break;
    case CallDescriptor::kCallJSFunction:
      inputs.push_back(
          UseLocation(callee_idx, call_descriptor->GetInputLocation(0)));
      break;
  }
  DCHECK_EQ(1, inputs.size());

  // TODO(tebbi): handle tail calls and frame states

  EmitPrepareArguments(op.arguments(), call_descriptor);
  UpdateMaxPushedArgumentCount(op.arguments().size());

  // Pass label of exception handler block.
  // if (handler) {
  //   DCHECK_EQ(IrOpcode::kIfException, handler->front()->opcode());
  //   flags |= CallDescriptor::kHasExceptionHandler;
  //   buffer.instruction_args.push_back(g.Label(handler));
  // }

  // Select the appropriate opcode based on the call type.
  InstructionCode opcode;
  switch (call_descriptor->kind()) {
    case CallDescriptor::kCallAddress: {
      int gp_param_count =
          static_cast<int>(call_descriptor->GPParameterCount());
      int fp_param_count =
          static_cast<int>(call_descriptor->FPParameterCount());
#if ABI_USES_FUNCTION_DESCRIPTORS
      // Highest fp_param_count bit is used on AIX to indicate if a CFunction
      // call has function descriptor or not.
      STATIC_ASSERT(FPParamField::kSize == kHasFunctionDescriptorBitShift + 1);
      if (!call_descriptor->NoFunctionDescriptor()) {
        fp_param_count |= 1 << kHasFunctionDescriptorBitShift;
      }
#endif
      opcode = kArchCallCFunction | ParamField::encode(gp_param_count) |
               FPParamField::encode(fp_param_count);
      break;
    }
    case CallDescriptor::kCallCodeObject:
      opcode = EncodeCallDescriptorFlags(kArchCallCodeObject, flags);
      break;
    case CallDescriptor::kCallJSFunction:
      opcode = EncodeCallDescriptorFlags(kArchCallJSFunction, flags);
      break;
#if V8_ENABLE_WEBASSEMBLY
    case CallDescriptor::kCallWasmCapiFunction:
    case CallDescriptor::kCallWasmFunction:
    case CallDescriptor::kCallWasmImportWrapper:
      opcode = EncodeCallDescriptorFlags(kArchCallWasmFunction, flags);
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    case CallDescriptor::kCallBuiltinPointer:
      opcode = EncodeCallDescriptorFlags(kArchCallBuiltinPointer, flags);
      break;
  }

  // Emit the call instruction.
  Instruction* call_instr =
      Emit(opcode, base::VectorOf(outputs), base::VectorOf(inputs));
  if (instruction_selection_failed) return;
  call_instr->MarkAsCall();

  EmitPrepareResults(base::VectorOf(results), call_descriptor);

  if (call_descriptor->NeedsCallerSavedRegisters()) {
    Emit(
        kArchRestoreCallerRegisters | MiscField::encode(static_cast<int>(mode)),
        {}, {});
  }
}

template <>
void InstructionSelector::Visit(const LoadStackCheckOffsetOp& op) {
  Emit(kArchStackCheckOffset, {DefineAsRegister(Index(op))}, {});
}

template <>
void InstructionSelector::Visit(const StackPointerGreaterThanOp& op) {
  VisitStackPointerGreaterThan(op, FlagsContinuation::ForSet(Index(op)));
}

void InstructionSelector::Visit(const Operation& op) {
  tick_counter->TickAndMaybeEnterSafepoint();
  current_effect_level = effect_level[Index(op)];
  switch (op.opcode) {
#define SWITCH_CASE(Name) \
  case Opcode::k##Name:   \
    return Visit(op.Cast<Name##Op>());
    TURBOSHAFT_OPERATION_LIST(SWITCH_CASE)
#undef SWITCH_CASE
    default:
      UNREACHABLE();
  }
}

Instruction* InstructionSelector::EmitWithContinuation(
    InstructionCode opcode, const FlagsContinuation& cont,
    base::Vector<const InstructionOperand> outputs,
    base::Vector<const InstructionOperand> inputs,
    base::Vector<const InstructionOperand> temps) {
  base::SmallVector<InstructionOperand, 8> new_inputs = inputs;
  base::SmallVector<InstructionOperand, 8> new_outputs = outputs;

  switch (cont.kind()) {
    using Kind = FlagsContinuation::Kind;
    case Kind::kBranch:
      opcode |= FlagsModeField::encode(kFlags_branch);
      new_inputs.push_back(UseLabel(cont.true_block()));
      new_inputs.push_back(UseLabel(cont.false_block()));
      break;
    case Kind::kSet:
      opcode |= FlagsModeField::encode(kFlags_set);
      new_outputs.push_back(DefineAsRegister(cont.result()));
      break;
  }
  return Emit(opcode, base::VectorOf(new_outputs), base::VectorOf(new_inputs),
              temps);
}

}  // namespace

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8
