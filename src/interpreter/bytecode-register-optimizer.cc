// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-register-optimizer.h"

namespace v8 {
namespace internal {
namespace interpreter {

const uint32_t BytecodeRegisterOptimizer::kInvalidEquivalenceId = kMaxUInt32;

// A class for tracking the state of a register. This class tracks
// which equivalence set a register is a member of and also whether a
// register is materialized in the bytecode stream.
class BytecodeRegisterOptimizer::RegisterInfo final : public ZoneObject {
 public:
  RegisterInfo(Zone* zone, Register reg, uint32_t equivalence_id,
               bool materialized, bool allocated)
      : register_(reg),
        equivalence_id_(equivalence_id),
        materialized_(materialized),
        allocated_(allocated),
        needs_flush_(false),
        var_in_reg_(nullptr),
        initialized_(false),
        patch_candidates_(zone),
        next_(this),
        prev_(this) {}
  RegisterInfo(const RegisterInfo&) = delete;
  RegisterInfo& operator=(const RegisterInfo&) = delete;

  void AddToEquivalenceSetOf(RegisterInfo* info);
  void MoveToNewEquivalenceSet(uint32_t equivalence_id, bool materialized,
                               Variable* var = nullptr);
  bool IsOnlyMemberOfEquivalenceSet() const;
  bool IsOnlyMaterializedMemberOfEquivalenceSet() const;
  bool IsInSameEquivalenceSet(RegisterInfo* info) const;

  // Get a member of the register's equivalence set that is allocated.
  // Returns itself if allocated, and nullptr if there is no unallocated
  // equivalent register.
  RegisterInfo* GetAllocatedEquivalent();

  // Get a member of this register's equivalence set that is
  // materialized. The materialized equivalent will be this register
  // if it is materialized. Returns nullptr if no materialized
  // equivalent exists.
  RegisterInfo* GetMaterializedEquivalent();

  // Get a member of this register's equivalence set that is
  // materialized and not register |reg|. The materialized equivalent
  // will be this register if it is materialized. Returns nullptr if
  // no materialized equivalent exists.
  RegisterInfo* GetMaterializedEquivalentOtherThan(Register reg);

  // Get a member of this register's equivalence set that is intended
  // to be materialized in place of this register (which is currently
  // materialized). The best candidate is deemed to be the register
  // with the lowest index as this permits temporary registers to be
  // removed from the bytecode stream. Returns nullptr if no candidate
  // exists.
  RegisterInfo* GetEquivalentToMaterialize();

  // Marks all temporary registers of the equivalence set as unmaterialized.
  void MarkTemporariesAsUnmaterialized(Register temporary_base);

  // Get an equivalent register. Returns this if none exists.
  RegisterInfo* GetEquivalent();

  Register register_value() const { return register_; }
  bool materialized() const { return materialized_; }
  void set_materialized(bool materialized) { materialized_ = materialized; }
  bool allocated() const { return allocated_; }
  void set_allocated(bool allocated) { allocated_ = allocated; }
  void set_equivalence_id(uint32_t equivalence_id) {
    equivalence_id_ = equivalence_id;
  }
  uint32_t equivalence_id() const { return equivalence_id_; }
  // Indicates if a register should be processed when calling Flush().
  bool needs_flush() const { return needs_flush_; }
  void set_needs_flush(bool needs_flush) { needs_flush_ = needs_flush; }
  Variable* var_in_reg() const { return var_in_reg_; }
  void set_var_in_reg(Variable* var) { var_in_reg_ = var; }

  // Indicates if a register is initialized after allocation
  bool initialized() const { return initialized_; }
  void set_initialized(bool initialized) { initialized_ = initialized; }
  ZoneVector<size_t>& patch_candidates() { return patch_candidates_; }

 private:
  Register register_;
  uint32_t equivalence_id_;
  bool materialized_;
  bool allocated_;
  bool needs_flush_;
  Variable* var_in_reg_;
  bool initialized_;
  // Lists the bytecode array indices where a register is used in one value
  // scope as patch candidates.
  ZoneVector<size_t> patch_candidates_;

  // Equivalence set pointers.
  RegisterInfo* next_;
  RegisterInfo* prev_;
};

void BytecodeRegisterOptimizer::RegisterInfo::AddToEquivalenceSetOf(
    RegisterInfo* info) {
  DCHECK_NE(kInvalidEquivalenceId, info->equivalence_id());
  // Fix old list
  next_->prev_ = prev_;
  prev_->next_ = next_;
  // Add to new list.
  next_ = info->next_;
  prev_ = info;
  prev_->next_ = this;
  next_->prev_ = this;
  set_equivalence_id(info->equivalence_id());
  set_materialized(false);
  set_var_in_reg(info->var_in_reg());
}

void BytecodeRegisterOptimizer::RegisterInfo::MoveToNewEquivalenceSet(
    uint32_t equivalence_id, bool materialized, Variable* var) {
  next_->prev_ = prev_;
  prev_->next_ = next_;
  next_ = prev_ = this;
  equivalence_id_ = equivalence_id;
  materialized_ = materialized;
  var_in_reg_ = var;
}

bool BytecodeRegisterOptimizer::RegisterInfo::IsOnlyMemberOfEquivalenceSet()
    const {
  return this->next_ == this;
}

void BytecodeRegisterOptimizer::SetVariableInRegister(Variable* var,
                                                      Register reg) {
  RegisterInfo* info = GetRegisterInfo(reg);
  PushToRegistersNeedingFlush(info);
  info->set_var_in_reg(var);
}

Variable* BytecodeRegisterOptimizer::GetVariableInRegister(Register reg) {
  RegisterInfo* info = GetRegisterInfo(reg);
  return info->var_in_reg();
}

bool BytecodeRegisterOptimizer::IsVariableInRegister(Variable* var,
                                                     Register reg) {
  DCHECK_NOT_NULL(var);
  RegisterInfo* info = GetRegisterInfo(reg);
  return info->var_in_reg() == var;
}

bool BytecodeRegisterOptimizer::RegisterInfo::
    IsOnlyMaterializedMemberOfEquivalenceSet() const {
  DCHECK(materialized());

  const RegisterInfo* visitor = this->next_;
  while (visitor != this) {
    if (visitor->materialized()) {
      return false;
    }
    visitor = visitor->next_;
  }
  return true;
}

bool BytecodeRegisterOptimizer::RegisterInfo::IsInSameEquivalenceSet(
    RegisterInfo* info) const {
  return equivalence_id() == info->equivalence_id();
}

BytecodeRegisterOptimizer::RegisterInfo*
BytecodeRegisterOptimizer::RegisterInfo::GetAllocatedEquivalent() {
  RegisterInfo* visitor = this;
  do {
    if (visitor->allocated()) {
      return visitor;
    }
    visitor = visitor->next_;
  } while (visitor != this);

  return nullptr;
}

BytecodeRegisterOptimizer::RegisterInfo*
BytecodeRegisterOptimizer::RegisterInfo::GetMaterializedEquivalent() {
  RegisterInfo* visitor = this;
  do {
    if (visitor->materialized()) {
      return visitor;
    }
    visitor = visitor->next_;
  } while (visitor != this);

  return nullptr;
}

BytecodeRegisterOptimizer::RegisterInfo*
BytecodeRegisterOptimizer::RegisterInfo::GetMaterializedEquivalentOtherThan(
    Register reg) {
  RegisterInfo* visitor = this;
  do {
    if (visitor->materialized() && visitor->register_value() != reg) {
      return visitor;
    }
    visitor = visitor->next_;
  } while (visitor != this);

  return nullptr;
}

BytecodeRegisterOptimizer::RegisterInfo*
BytecodeRegisterOptimizer::RegisterInfo::GetEquivalentToMaterialize() {
  DCHECK(this->materialized());
  RegisterInfo* visitor = this->next_;
  RegisterInfo* best_info = nullptr;
  while (visitor != this) {
    if (visitor->materialized()) {
      return nullptr;
    }
    if (visitor->allocated() &&
        (best_info == nullptr ||
         visitor->register_value() < best_info->register_value())) {
      best_info = visitor;
    }
    visitor = visitor->next_;
  }
  return best_info;
}

void BytecodeRegisterOptimizer::RegisterInfo::MarkTemporariesAsUnmaterialized(
    Register temporary_base) {
  DCHECK(this->register_value() < temporary_base);
  DCHECK(this->materialized());
  RegisterInfo* visitor = this->next_;
  while (visitor != this) {
    if (visitor->register_value() >= temporary_base) {
      visitor->set_materialized(false);
    }
    visitor = visitor->next_;
  }
}

BytecodeRegisterOptimizer::RegisterInfo*
BytecodeRegisterOptimizer::RegisterInfo::GetEquivalent() {
  return next_;
}

BytecodeRegisterOptimizer::BytecodeRegisterOptimizer(
    Zone* zone, BytecodeRegisterAllocator* register_allocator,
    int fixed_registers_count, int parameter_count,
    BytecodeWriter* bytecode_writer)
    : accumulator_(Register::virtual_accumulator()),
      temporary_base_(fixed_registers_count),
      max_register_index_(fixed_registers_count - 1),
      register_info_table_(zone),
      registers_needing_flushed_(zone),
      equivalence_id_(0),
      bytecode_writer_(bytecode_writer),
      flush_required_(false),
      zone_(zone) {
  register_allocator->set_observer(this);

  // Calculate offset so register index values can be mapped into
  // a vector of register metadata.
  // There is at least one parameter, which is the JS receiver.
  DCHECK_NE(parameter_count, 0);
  int first_slot_index = parameter_count - 1;
  register_info_table_offset_ =
      -Register::FromParameterIndex(first_slot_index).index();

  // Initialize register map for parameters, locals, and the
  // accumulator.
  register_info_table_.resize(register_info_table_offset_ +
                              static_cast<size_t>(temporary_base_.index()));
  for (size_t i = 0; i < register_info_table_.size(); ++i) {
    register_info_table_[i] =
        zone->New<RegisterInfo>(zone_, RegisterFromRegisterInfoTableIndex(i),
                                NextEquivalenceId(), true, true);
    DCHECK_EQ(register_info_table_[i]->register_value().index(),
              RegisterFromRegisterInfoTableIndex(i).index());
  }
  accumulator_info_ = GetRegisterInfo(accumulator_);
  DCHECK(accumulator_info_->register_value() == accumulator_);
}

void BytecodeRegisterOptimizer::PushToRegistersNeedingFlush(RegisterInfo* reg) {
  // Flushing is required in two cases:
  // 1) Two or more registers in the same equivalence set.
  // 2) Binding a variable to a register.
  flush_required_ = true;
  if (!reg->needs_flush()) {
    reg->set_needs_flush(true);
    registers_needing_flushed_.push_back(reg);
  }
}

bool BytecodeRegisterOptimizer::EnsureAllRegistersAreFlushed() const {
  for (RegisterInfo* reg_info : register_info_table_) {
    if (reg_info->needs_flush()) {
      return false;
    } else if (!reg_info->IsOnlyMemberOfEquivalenceSet()) {
      return false;
    } else if (reg_info->allocated() && !reg_info->materialized()) {
      return false;
    }
  }
  return true;
}

void BytecodeRegisterOptimizer::Flush() {
  if (!flush_required_) {
    return;
  }

  // Materialize all live registers and break equivalences.
  for (RegisterInfo* reg_info : registers_needing_flushed_) {
    if (!reg_info->needs_flush()) continue;
    reg_info->set_needs_flush(false);
    reg_info->set_var_in_reg(nullptr);

    RegisterInfo* materialized = reg_info->materialized()
                                     ? reg_info
                                     : reg_info->GetMaterializedEquivalent();

    if (materialized != nullptr) {
      // Walk equivalents of materialized registers, materializing
      // each equivalent register as necessary and placing in their
      // own equivalence set.
      RegisterInfo* equivalent;
      while ((equivalent = materialized->GetEquivalent()) != materialized) {
        if (equivalent->allocated() && !equivalent->materialized()) {
          OutputRegisterTransfer(materialized, equivalent);
        }
        equivalent->MoveToNewEquivalenceSet(NextEquivalenceId(), true);
        equivalent->set_needs_flush(false);
      }
    } else {
      // Equivalence class containing only unallocated registers.
      DCHECK_NULL(reg_info->GetAllocatedEquivalent());
      reg_info->MoveToNewEquivalenceSet(NextEquivalenceId(), false);
    }
  }

  registers_needing_flushed_.clear();
  DCHECK(EnsureAllRegistersAreFlushed());

  flush_required_ = false;
}

void BytecodeRegisterOptimizer::OutputRegisterTransfer(
    RegisterInfo* input_info, RegisterInfo* output_info) {
  Register input = input_info->register_value();
  Register output = output_info->register_value();
  DCHECK_NE(input.index(), output.index());

  if (input == accumulator_) {
    output_info->set_initialized(true);
    output_info->patch_candidates().clear();
    bytecode_writer_->EmitStar(output);
  } else if (output == accumulator_) {
    bytecode_writer_->EmitLdar(input);
  } else {
    output_info->set_initialized(true);
    output_info->patch_candidates().clear();
    bytecode_writer_->EmitMov(input, output);
  }
  if (output != accumulator_) {
    max_register_index_ = std::max(max_register_index_, output.index());
  }
  output_info->set_materialized(true);
}

void BytecodeRegisterOptimizer::CreateMaterializedEquivalent(
    RegisterInfo* info) {
  DCHECK(info->materialized());
  RegisterInfo* unmaterialized = info->GetEquivalentToMaterialize();
  if (unmaterialized) {
    OutputRegisterTransfer(info, unmaterialized);
  }
}

BytecodeRegisterOptimizer::RegisterInfo*
BytecodeRegisterOptimizer::GetMaterializedEquivalent(RegisterInfo* info) {
  return info->materialized() ? info : info->GetMaterializedEquivalent();
}

BytecodeRegisterOptimizer::RegisterInfo*
BytecodeRegisterOptimizer::GetMaterializedEquivalentNotAccumulator(
    RegisterInfo* info) {
  if (info->materialized()) {
    return info;
  }

  RegisterInfo* result = info->GetMaterializedEquivalentOtherThan(accumulator_);
  if (result == nullptr) {
    Materialize(info);
    result = info;
  }
  DCHECK(result->register_value() != accumulator_);
  return result;
}

void BytecodeRegisterOptimizer::Materialize(RegisterInfo* info) {
  if (!info->materialized()) {
    RegisterInfo* materialized = info->GetMaterializedEquivalent();
    DCHECK_NOT_NULL(materialized);
    OutputRegisterTransfer(materialized, info);
  }
}

void BytecodeRegisterOptimizer::AddToEquivalenceSet(
    RegisterInfo* set_member, RegisterInfo* non_set_member) {
  // Equivalence class is now of size >= 2, so we make sure it will be flushed.
  PushToRegistersNeedingFlush(non_set_member);
  non_set_member->AddToEquivalenceSetOf(set_member);
}

void BytecodeRegisterOptimizer::RegisterTransfer(RegisterInfo* input_info,
                                                 RegisterInfo* output_info) {
  bool output_is_observable =
      RegisterIsObservable(output_info->register_value());
  bool in_same_equivalence_set =
      output_info->IsInSameEquivalenceSet(input_info);
  if (in_same_equivalence_set &&
      (!output_is_observable || output_info->materialized())) {
    return;  // Nothing more to do.
  }

  // Materialize an alternate in the equivalence set that
  // |output_info| is leaving.
  if (output_info->materialized()) {
    CreateMaterializedEquivalent(output_info);
  }

  // Add |output_info| to new equivalence set.
  if (!in_same_equivalence_set) {
    AddToEquivalenceSet(input_info, output_info);
  }

  if (output_is_observable) {
    // Force store to be emitted when register is observable.
    output_info->set_materialized(false);
    RegisterInfo* materialized_info = input_info->GetMaterializedEquivalent();
    // TODO(yolandachen): expand the patch to all movs.
    if (output_info->initialized() ||
        !TryPatchCandidate(materialized_info, output_info)) {
      OutputRegisterTransfer(materialized_info, output_info);
    } else {
      // Skip the transfer as input operand is patched to output already
      output_info->set_materialized(true);
      // Input maybe referenced later. Mark all temporaries as unmaterialized so
      // that the observable register is used in preference.
      output_info->MarkTemporariesAsUnmaterialized(temporary_base_);
    }
    if (!output_info->initialized()) output_info->set_initialized(true);
  }

  bool input_is_observable = RegisterIsObservable(input_info->register_value());
  if (input_is_observable) {
    // If input is observable by the debugger, mark all other temporaries
    // registers as unmaterialized so that this register is used in preference.
    input_info->MarkTemporariesAsUnmaterialized(temporary_base_);
  }
}

void BytecodeRegisterOptimizer::PrepareOutputRegister(Register reg) {
  RegisterInfo* reg_info = GetRegisterInfo(reg);
  if (reg_info->materialized()) {
    CreateMaterializedEquivalent(reg_info);
  }
  reg_info->MoveToNewEquivalenceSet(NextEquivalenceId(), true);
  max_register_index_ =
      std::max(max_register_index_, reg_info->register_value().index());
  reg_info->set_initialized(true);
  reg_info->patch_candidates().clear();
}

void BytecodeRegisterOptimizer::PrepareOutputRegisterList(
    RegisterList reg_list) {
  int start_index = reg_list.first_register().index();
  for (int i = 0; i < reg_list.register_count(); ++i) {
    Register current(start_index + i);
    PrepareOutputRegister(current);
    // We cannot optimize move if it is used in register list
    if (reg_list.register_count() > 1 && RegisterIsTemporary(current)) {
      RegisterInfo* curr_info = GetRegisterInfo(current);
      curr_info->set_initialized(false);
    }
  }
}

Register BytecodeRegisterOptimizer::GetInputRegister(Register reg) {
  RegisterInfo* reg_info = GetRegisterInfo(reg);
  if (reg_info->materialized()) {
    return reg;
  } else {
    RegisterInfo* equivalent_info =
        GetMaterializedEquivalentNotAccumulator(reg_info);
    return equivalent_info->register_value();
  }
}

RegisterList BytecodeRegisterOptimizer::GetInputRegisterList(
    RegisterList reg_list) {
  if (reg_list.register_count() == 1) {
    // If there is only a single register, treat it as a normal input register.
    Register reg(GetInputRegister(reg_list.first_register()));
    return RegisterList(reg);
  } else {
    int start_index = reg_list.first_register().index();
    for (int i = 0; i < reg_list.register_count(); ++i) {
      Register current(start_index + i);
      RegisterInfo* input_info = GetRegisterInfo(current);
      Materialize(input_info);
      // We cannot optimize move if it is used in register list
      input_info->set_initialized(false);
    }
    return reg_list;
  }
}

void BytecodeRegisterOptimizer::GrowRegisterMap(Register reg) {
  DCHECK(RegisterIsTemporary(reg));
  size_t index = GetRegisterInfoTableIndex(reg);
  if (index >= register_info_table_.size()) {
    size_t new_size = index + 1;
    size_t old_size = register_info_table_.size();
    register_info_table_.resize(new_size);
    for (size_t i = old_size; i < new_size; ++i) {
      register_info_table_[i] = zone()->New<RegisterInfo>(
          zone_, RegisterFromRegisterInfoTableIndex(i), NextEquivalenceId(),
          true, false);
    }
  }
}

void BytecodeRegisterOptimizer::AllocateRegister(RegisterInfo* info) {
  info->set_allocated(true);
  if (!info->materialized()) {
    info->MoveToNewEquivalenceSet(NextEquivalenceId(), true);
  }
}

void BytecodeRegisterOptimizer::RegisterAllocateEvent(Register reg) {
  AllocateRegister(GetOrCreateRegisterInfo(reg));
}

void BytecodeRegisterOptimizer::RegisterListAllocateEvent(
    RegisterList reg_list) {
  if (reg_list.register_count() != 0) {
    int first_index = reg_list.first_register().index();
    GrowRegisterMap(Register(first_index + reg_list.register_count() - 1));
    for (int i = 0; i < reg_list.register_count(); i++) {
      AllocateRegister(GetRegisterInfo(Register(first_index + i)));
    }
  }
}

void BytecodeRegisterOptimizer::RegisterListFreeEvent(RegisterList reg_list) {
  int first_index = reg_list.first_register().index();
  for (int i = 0; i < reg_list.register_count(); i++) {
    RegisterFreeEvent(Register(first_index + i));
  }
}

void BytecodeRegisterOptimizer::RegisterFreeEvent(Register reg) {
  GetRegisterInfo(reg)->set_allocated(false);
}

void BytecodeRegisterOptimizer::AddPatchCandidates(uint32_t operand,
                                                   size_t offset) {
  Register reg = Register::FromOperand(static_cast<int32_t>(operand));
  if (reg.index() != 0 && RegisterIsTemporary(reg)) {
    RegisterInfo* info = GetRegisterInfo(reg);
    info->patch_candidates().emplace_back(offset);
  }
}

void BytecodeRegisterOptimizer::AddPatchSta(Register reg, size_t offset) {
  if (RegisterIsTemporary(reg)) {
    RegisterInfo* info = GetRegisterInfo(reg);
    info->patch_candidates().emplace_back(offset);
  }
}

bool BytecodeRegisterOptimizer::TryPatchCandidate(RegisterInfo* input,
                                                  RegisterInfo* output) {
  DCHECK(!output->initialized());
  int dst = output->register_value().index();
  if (dst < 0 || input->patch_candidates().empty() || !input->initialized() ||
      input->register_value().SizeOfOperand() !=
          output->register_value().SizeOfOperand()) {
    return false;
  }

  // Patch code
  int src = input->register_value().index();
  DCHECK(src > dst);
  bytecode_writer_->PatchOperands(input->patch_candidates(),
                                  static_cast<uint8_t>(src - dst));
  return true;
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
