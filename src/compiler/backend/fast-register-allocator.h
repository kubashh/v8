// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_FAST_REGISTER_ALLOCATOR_H_
#define V8_COMPILER_BACKEND_FAST_REGISTER_ALLOCATOR_H_

#include "src/base/bits.h"
#include "src/base/compiler-specific.h"
#include "src/base/threaded-list.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/register-configuration.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/backend/register-allocation.h"
#include "src/flags/flags.h"
#include "src/utils/bit-vector.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class TickCounter;

namespace compiler {

class SinglePassRegisterAllocator;
class VirtualRegisterData;
class BlockState;

class FastRegisterAllocatorData final : public RegisterAllocationData {
 public:
  FastRegisterAllocatorData(const RegisterConfiguration* config,
                            Zone* allocation_zone, Frame* frame,
                            InstructionSequence* code,
                            TickCounter* tick_counter,
                            const char* debug_name = nullptr);

  static FastRegisterAllocatorData* cast(RegisterAllocationData* data) {
    DCHECK_EQ(data->type(), Type::kFastRegisterAllocation);
    return static_cast<FastRegisterAllocatorData*>(data);
  }

  VirtualRegisterData& VirtualRegisterDataFor(int virtual_register);
  MachineRepresentation RepresentationFor(int virtual_register);

  // Add a gap move between the given operands |from| and |to|.
  MoveOperands* AddGapMove(int instr_index, Instruction::GapPosition position,
                           const InstructionOperand& from,
                           const InstructionOperand& to);

  // Adds a gap move where both sides are PendingOperand operands.
  MoveOperands* AddPendingOperandGapMove(int instr_index,
                                         Instruction::GapPosition position);

  // Helpers to get a block from an "rpo_number| or |instr_index|.
  const InstructionBlock* GetBlock(const RpoNumber rpo_number);
  const InstructionBlock* GetBlock(int instr_index);

  // Returns a bitvector representing all the blocks that are dominated by the
  // an output by the instruction at |instr_index|.
  const BitVector* GetBlocksDominatedBy(int instr_index);

  // List of all instruction indexs that require a reference map.
  ZoneVector<int>& reference_map_instructions() {
    return reference_map_instructions_;
  }

  // Returns a bitvector representing the virtual registers that were spilled.
  BitVector& spilled_virtual_registers() { return spilled_virtual_registers_; }

  // This zone is for data structures only needed during register allocation
  // phases.
  Zone* allocation_zone() const { return allocation_zone_; }

  // This zone is for InstructionOperands and moves that live beyond register
  // allocation.
  Zone* code_zone() const { return code()->zone(); }

  BlockState& block_state(RpoNumber rpo_number);
  InstructionSequence* code() const { return code_; }
  Frame* frame() const { return frame_; }
  const char* debug_name() const { return debug_name_; }
  const RegisterConfiguration* config() const { return config_; }
  TickCounter* tick_counter() { return tick_counter_; }

 private:
  Zone* const allocation_zone_;
  Frame* const frame_;
  InstructionSequence* const code_;
  const char* const debug_name_;
  const RegisterConfiguration* const config_;

  ZoneVector<VirtualRegisterData> virtual_register_data_;
  ZoneVector<BlockState> block_state_;
  ZoneVector<int> reference_map_instructions_;
  BitVector spilled_virtual_registers_;

  TickCounter* const tick_counter_;

  DISALLOW_COPY_AND_ASSIGN(FastRegisterAllocatorData);
};

// This is a register allocator specifically designed to perform register
// allocation as fast as possible while minimizing spill moves.
class FastRegisterAllocator final {
 public:
  explicit FastRegisterAllocator(FastRegisterAllocatorData* data);
  ~FastRegisterAllocator();

  // Phase 1: Process instruction outputs to determine
  void DefineOutputs();

  // Phase 2: allocate register to instructions.
  void AllocateRegisters();

 private:
  // Define outputs operations.
  void InitializeBlockState(const InstructionBlock* block);
  void DefineOutputs(const InstructionBlock* block);

  // Allocate registers operations.
  void AllocateRegisters(const InstructionBlock* block);
  void AllocatePhis(const InstructionBlock* block);
  void AllocatePhiGapMoves(const InstructionBlock* block);
  void UpdateSpillRangesForLoops();

  bool IsFixedRegisterPolicy(const UnallocatedOperand* operand);
  void ReserveFixedRegisters(int instr_index);

  SinglePassRegisterAllocator& AllocatorFor(MachineRepresentation rep);
  SinglePassRegisterAllocator& AllocatorFor(const UnallocatedOperand* operand);
  SinglePassRegisterAllocator& AllocatorFor(const ConstantOperand* operand);

  SinglePassRegisterAllocator& general_reg_allocator() {
    return *general_reg_allocator_;
  }
  SinglePassRegisterAllocator& double_reg_allocator() {
    return *double_reg_allocator_;
  }

  VirtualRegisterData& VirtualRegisterDataFor(int virtual_register) const {
    return data()->VirtualRegisterDataFor(virtual_register);
  }
  MachineRepresentation RepresentationFor(int virtual_register) const {
    return data()->RepresentationFor(virtual_register);
  }
  FastRegisterAllocatorData* data() const { return data_; }
  InstructionSequence* code() const { return data()->code(); }
  Zone* allocation_zone() const { return data()->allocation_zone(); }

  FastRegisterAllocatorData* data_;
  std::unique_ptr<SinglePassRegisterAllocator> general_reg_allocator_;
  std::unique_ptr<SinglePassRegisterAllocator> double_reg_allocator_;

  DISALLOW_COPY_AND_ASSIGN(FastRegisterAllocator);
};

// Spill slot allocator for fast register allocation.
class FastSpillSlotAllocator final {
 public:
  explicit FastSpillSlotAllocator(FastRegisterAllocatorData* data);

  // Phase 3: assign spilled operands to specific spill slots.
  void AllocateSpillSlots();

 private:
  class SpillSlot;

  void Allocate(VirtualRegisterData* virtual_register);

  void AdvanceTo(int instr_index);
  SpillSlot* GetFreeSpillSlot(int byte_width);

  FastRegisterAllocatorData* data() const { return data_; }
  InstructionSequence* code() const { return data()->code(); }
  Frame* frame() const { return data()->frame(); }
  Zone* zone() const { return data()->allocation_zone(); }

  struct OrderByLastUse {
    bool operator()(const SpillSlot* a, const SpillSlot* b) const;
  };

  FastRegisterAllocatorData* data_;
  ZonePriorityQueue<SpillSlot*, OrderByLastUse> allocated_slots_;
  ZoneLinkedList<SpillSlot*> free_slots_;
  int position_;

  DISALLOW_COPY_AND_ASSIGN(FastSpillSlotAllocator);
};

// Populates reference maps for fast register allocation.
class FastReferenceMapPopulator final {
 public:
  explicit FastReferenceMapPopulator(FastRegisterAllocatorData* data);

  // Phase 4: Populate reference maps for spilled references.
  void PopulateReferenceMaps();

 private:
  void RecordReferences(const VirtualRegisterData& virtual_register);

  FastRegisterAllocatorData* data() const { return data_; }
  InstructionSequence* code() const { return data()->code(); }

  FastRegisterAllocatorData* data_;

  DISALLOW_COPY_AND_ASSIGN(FastReferenceMapPopulator);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_FAST_REGISTER_ALLOCATOR_H_
