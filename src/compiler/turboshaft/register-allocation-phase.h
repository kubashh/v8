// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_REGISTER_ALLOCATION_PHASE_H_
#define V8_COMPILER_TURBOSHAFT_REGISTER_ALLOCATION_PHASE_H_

#include "src/compiler/backend/move-optimizer.h"
#include "src/compiler/backend/register-allocator.h"
#include "src/compiler/pipeline-data-inl.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/pipelines.h"
#include "src/compiler/backend/frame-elider.h"
#include "src/compiler/backend/jump-threading.h"

namespace v8::internal::compiler::turboshaft {

struct MeetRegisterConstraintsPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(MeetRegisterConstraints)
  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    ConstraintBuilder builder(data->register_allocation_data());
    builder.MeetRegisterConstraints();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    ConstraintBuilder builder(data.register_allocation_data);
    builder.MeetRegisterConstraints();
  }
};

struct ResolvePhisPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(ResolvePhis)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    ConstraintBuilder builder(data->register_allocation_data());
    builder.ResolvePhis();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    ConstraintBuilder builder(data.register_allocation_data);
    builder.ResolvePhis();
  }
};

struct BuildLiveRangesPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(BuildLiveRanges)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    LiveRangeBuilder builder(data->register_allocation_data(), temp_zone);
    builder.BuildLiveRanges();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    LiveRangeBuilder builder(data.register_allocation_data, temp_zone);
    builder.BuildLiveRanges();
  }
};

struct BuildBundlesPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(BuildLiveRangeBundles)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    BundleBuilder builder(data->register_allocation_data());
    builder.BuildBundles();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    BundleBuilder builder(data.register_allocation_data);
    builder.BuildBundles();
  }
};

template <typename RegAllocator>
struct AllocateGeneralRegistersPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(AllocateGeneralRegisters)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    RegAllocator allocator(data->register_allocation_data(),
                           RegisterKind::kGeneral, temp_zone);
    allocator.AllocateRegisters();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    RegAllocator allocator(data.register_allocation_data,
                           RegisterKind::kGeneral, temp_zone);
    allocator.AllocateRegisters();
  }
};

template <typename RegAllocator>
struct AllocateFPRegistersPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(AllocateFPRegisters)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    RegAllocator allocator(data->register_allocation_data(),
                           RegisterKind::kDouble, temp_zone);
    allocator.AllocateRegisters();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    RegAllocator allocator(data.register_allocation_data, RegisterKind::kDouble,
                           temp_zone);
    allocator.AllocateRegisters();
  }
};

template <typename RegAllocator>
struct AllocateSimd128RegistersPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(AllocateSIMD128Registers)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    RegAllocator allocator(data->register_allocation_data(),
                           RegisterKind::kSimd128, temp_zone);
    allocator.AllocateRegisters();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    RegAllocator allocator(data.register_allocation_data,
                           RegisterKind::kSimd128, temp_zone);
    allocator.AllocateRegisters();
  }
};

struct DecideSpillingModePhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(DecideSpillingMode)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    OperandAssigner assigner(data->register_allocation_data());
    assigner.DecideSpillingMode();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    OperandAssigner assigner(data.register_allocation_data);
    assigner.DecideSpillingMode();
  }
};

struct AssignSpillSlotsPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(AssignSpillSlots)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    OperandAssigner assigner(data->register_allocation_data());
    assigner.AssignSpillSlots();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    OperandAssigner assigner(data.register_allocation_data);
    assigner.AssignSpillSlots();
  }
};

struct CommitAssignmentPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(CommitAssignment)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    OperandAssigner assigner(data->register_allocation_data());
    assigner.CommitAssignment();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    OperandAssigner assigner(data.register_allocation_data);
    assigner.CommitAssignment();
  }
};

struct PopulateReferenceMapsPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(PopulatePointerMaps)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    ReferenceMapPopulator populator(data->register_allocation_data());
    populator.PopulateReferenceMaps();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    ReferenceMapPopulator populator(data.register_allocation_data);
    populator.PopulateReferenceMaps();
  }
};

struct ConnectRangesPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(ConnectRanges)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    LiveRangeConnector connector(data->register_allocation_data());
    connector.ConnectRanges(temp_zone);
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    LiveRangeConnector connector(data.register_allocation_data);
    connector.ConnectRanges(temp_zone);
  }
};

struct ResolveControlFlowPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(ResolveControlFlow)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    LiveRangeConnector connector(data->register_allocation_data());
    connector.ResolveControlFlow(temp_zone);
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    RegisterAllocatorData& data =
        data_provider->GetDataComponent<RegisterAllocatorData>();
    LiveRangeConnector connector(data.register_allocation_data);
    connector.ResolveControlFlow(temp_zone);
  }
};

struct OptimizeMovesPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(OptimizeMoves)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    MoveOptimizer move_optimizer(temp_zone, data->sequence());
    move_optimizer.Run();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    InstructionSequenceData& data =
        data_provider->GetDataComponent<InstructionSequenceData>();
    MoveOptimizer move_optimizer(temp_zone, data.sequence);
    move_optimizer.Run();
  }
};

struct FrameElisionPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(FrameElision)

  void Run(compiler::PipelineData* data, Zone* temp_zone, bool has_dummy_end_block) {
    FrameElider(data->sequence(), has_dummy_end_block).Run();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone, bool has_dummy_end_block) {
    InstructionSequenceData& data =
        data_provider->GetDataComponent<InstructionSequenceData>();
    FrameElider(data.sequence, has_dummy_end_block).Run();
  }
};

struct JumpThreadingPhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(JumpThreading)

  void Run(compiler::PipelineData* data, Zone* temp_zone, bool frame_at_start) {
    ZoneVector<RpoNumber> result(temp_zone);
    if (JumpThreading::ComputeForwarding(temp_zone, &result, data->sequence(),
                                         frame_at_start)) {
      JumpThreading::ApplyForwarding(temp_zone, result, data->sequence());
    }
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone, bool frame_at_start) {
    InstructionSequenceData& data =
        data_provider->GetDataComponent<InstructionSequenceData>();
    ZoneVector<RpoNumber> result(temp_zone);
    if (JumpThreading::ComputeForwarding(temp_zone, &result, data.sequence,
                                         frame_at_start)) {
      JumpThreading::ApplyForwarding(temp_zone, result, data.sequence);
    }
  }
};

struct AssembleCodePhase : public Phase<false> {
  DECL_PIPELINE_PHASE_CONSTANTS(AssembleCode)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    data->code_generator()->AssembleCode();
  }

  void Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    CodegenData& codegen_data = data_provider->GetDataComponent<CodegenData>();
    DCHECK_NOT_NULL(codegen_data.code_generator);
    codegen_data.code_generator->AssembleCode();
  }
};

struct FinalizeCodePhase : public Phase<false> {
  DECL_MAIN_THREAD_PIPELINE_PHASE_CONSTANTS(FinalizeCode)

  void Run(compiler::PipelineData* data, Zone* temp_zone) {
    data->set_code(data->code_generator()->FinalizeCode());
  }

  MaybeHandle<Code> Run(DataComponentProvider* data_provider, Zone* temp_zone) {
    CodegenData& codegen_data = data_provider->GetDataComponent<CodegenData>();
    DCHECK(codegen_data.code.is_null());
    return codegen_data.code_generator->FinalizeCode();
  }
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_REGISTER_ALLOCATION_PHASE_H_
