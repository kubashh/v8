// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/pipelines.h"

#include "src/compiler/turboshaft/build-graph-phase.h"
#include "src/compiler/turboshaft/code-elimination-and-simplification-phase.h"
#include "src/compiler/turboshaft/csa-optimize-phase.h"
#include "src/compiler/turboshaft/decompression-optimization-phase.h"

namespace v8::internal::compiler::turboshaft {

base::Optional<BailoutReason> PipelineBase::InitializeFromTurbofanGraph(
    Schedule* turbofan_graph, CallDescriptor* call_descriptor) {
  Linkage linkage(call_descriptor);
  return Run<BuildGraphPhase>(turbofan_graph, &linkage);
}

void BuiltinPipeline::RunOptimizations() {
  //    turboshaft::Tracing::Scope tracing_scope(&info_);
  //
  //    UnparkedScopeIfNeeded scope(data_.broker(),
  //                                v8_flags.turboshaft_trace_reduction);
  //    turboshaft::PipelineData::Scope turboshaft_scope(
  //        data_.GetTurboshaftPipelineData(
  //            turboshaft::TurboshaftPipelineKind::kCSA, graph));
  //

  Run<CsaEarlyMachineOptimizationPhase>();
  Run<CsaLoadEliminationPhase>();
  Run<CsaLateEscapeAnalysisPhase>();
  Run<CsaBranchEliminationPhase>();
  Run<CsaOptimizePhase>();
}

void BuiltinPipeline::RunInstructionSelection() {
  Run<CodeEliminationAndSimplificationPhase>();

  // DecompressionOptimization has to run as the last phase because it
  // constructs an (slightly) invalid graph that mixes Tagged and Compressed
  // representations.
  Run<DecompressionOptimizationPhase>();
}

}  // namespace v8::internal::compiler::turboshaft
