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

  // List of all instruction indexs that require a reference map.
  ZoneVector<int>& reference_map_instructions() {
    return reference_map_instructions_;
  }

  // This zone is for data structures only needed during register allocation
  // phases.
  Zone* allocation_zone() const { return allocation_zone_; }

  // This zone is for InstructionOperands and moves that live beyond register
  // allocation.
  Zone* code_zone() const { return code()->zone(); }

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
  ZoneVector<int> reference_map_instructions_;

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

  // TODO(rmcilroy): Phase 2 - allocate registers to instructions.

 private:
  // Define outputs operations.
  void InitializeBlockState(const InstructionBlock* block);
  void DefineOutputs(const InstructionBlock* block);

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

  DISALLOW_COPY_AND_ASSIGN(FastRegisterAllocator);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_FAST_REGISTER_ALLOCATOR_H_
