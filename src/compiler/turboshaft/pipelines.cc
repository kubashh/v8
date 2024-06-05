// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/pipelines.h"

#include "src/compiler/pipeline-data-inl.h"
#include "src/compiler/turboshaft/recreate-schedule-phase.h"
#include "src/compiler/turboshaft/csa-optimize-phase.h"

namespace v8::internal::compiler::turboshaft {

void Pipeline::RecreateTurbofanGraph(compiler::TFPipelineData* turbofan_data,
                                     Linkage* linkage) {
  Run<turboshaft::RecreateSchedulePhase>(turbofan_data, linkage);
  TraceSchedule(turbofan_data->info(), turbofan_data, turbofan_data->schedule(),
                turboshaft::RecreateSchedulePhase::phase_name());
}

void Pipeline::GenerateCode(Linkage* linkage, std::shared_ptr<OsrHelper> osr_helper,
    JumpOptimizationInfo* jump_optimization_info, int initial_graph_hash) {
    // Run code generation. If we optimize jumps, we repeat this a second time.
    data()->InitializeCodegenComponent(osr_helper,
                                               jump_optimization_info);

    // Perform instruction selection and register allocation.
    CHECK(SelectInstructions(linkage));
    CHECK(AllocateRegisters(linkage->GetIncomingDescriptor()));
    AssembleCode(linkage);

    if (v8_flags.turbo_profiling) {
      info()->profiler_data()->SetHash(initial_graph_hash);
    }

    if (jump_optimization_info && jump_optimization_info->is_optimizable()) {
      // Reset data for a second run of instruction selection.
      data()->ClearCodegenComponent();
      jump_optimization_info->set_optimizing();

      // Perform instruction selection and register allocation.
      data()->InitializeCodegenComponent(osr_helper, jump_optimization_info);
      SelectInstructions(linkage);
      AllocateRegisters(linkage->GetIncomingDescriptor());
      // Generate the final machine code.
      AssembleCode(linkage);
    }
}

void BuiltinPipeline::OptimizeBuiltin() {
    Tracing::Scope tracing_scope(data()->info());

    Run<CsaEarlyMachineOptimizationPhase>();
    Run<CsaLoadEliminationPhase>();
    Run<CsaLateEscapeAnalysisPhase>();
    Run<CsaBranchEliminationPhase>();
    Run<CsaOptimizePhase>();

    Run<CodeEliminationAndSimplificationPhase>();

    // DecompressionOptimization has to run as the last phase because it
    // constructs an (slightly) invalid graph that mixes Tagged and Compressed
    // representations.
    Run<DecompressionOptimizationPhase>();

#if 0
    // Run a first round of code generation, in order to be able
    // to repeat it for jump optimization.
    DCHECK_NULL(data.frame());
    turboshaft_data.InitializeCodegenComponent(data.osr_helper_ptr(),
                                               jump_optimization_info);

    CHECK(turboshaft_pipeline.SelectInstructions(&linkage));
    CHECK(
        turboshaft_pipeline.AllocateRegisters(linkage.GetIncomingDescriptor()));

    turboshaft_pipeline.AssembleCode(&linkage);

    if (v8_flags.turbo_profiling) {
      info.profiler_data()->SetHash(initial_graph_hash);
    }

    if (jump_opt.is_optimizable()) {
      // Reset data for a second run of instruction selection.
      turboshaft_data.ClearCodegenComponent();

      jump_opt.set_optimizing();

      // Perform instruction selection and register allocation.
      turboshaft_data.InitializeCodegenComponent(data.osr_helper_ptr(),
                                                 jump_optimization_info);
      turboshaft_pipeline.SelectInstructions(&linkage);
      turboshaft_pipeline.AllocateRegisters(linkage.GetIncomingDescriptor());

      // Generate the final machine code.
      turboshaft_pipeline.AssembleCode(&linkage);

      return turboshaft_pipeline.FinalizeCode();
    } else {
      return turboshaft_pipeline.FinalizeCode();
    }
#endif
  }
}  // namespace v8::internal::compiler::turboshaft
