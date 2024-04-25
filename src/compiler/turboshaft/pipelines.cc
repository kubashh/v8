// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/pipelines.h"

#include "src/codegen/optimized-compilation-info.h"
#include "src/codegen/register-configuration.h"
#include "src/compiler/backend/register-allocator-verifier.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/turboshaft/build-graph-phase.h"
#include "src/compiler/turboshaft/code-elimination-and-simplification-phase.h"
#include "src/compiler/turboshaft/csa-optimize-phase.h"
#include "src/compiler/turboshaft/decompression-optimization-phase.h"
#include "src/compiler/turboshaft/instruction-selection-phase.h"
#include "src/compiler/turboshaft/register-allocation-phase.h"

namespace v8::internal::compiler::turboshaft {

#if 0
base::Optional<BailoutReason> PipelineBase::InitializeFromTurbofanGraph(
    Schedule* turbofan_graph, CallDescriptor* call_descriptor) {
  Linkage linkage(call_descriptor);
  return Run<BuildGraphPhase>(turbofan_graph, &linkage);
}
#endif

base::Optional<BailoutReason> BuiltinPipeline::RunGraphConstructonFromTurbofan(
    Schedule* schedule, SourcePositionTable* source_positions,
    NodeOriginTable* node_origins, Linkage* linkage) {
  return Run<BuildGraphPhase>(schedule, source_positions, node_origins,
                              linkage);
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

bool BuiltinPipeline::RunInstructionSelection(
    Linkage* linkage, const AssemblerOptions& assembler_options) {
  Run<CodeEliminationAndSimplificationPhase>();

  // DecompressionOptimization has to run as the last phase because it
  // constructs an (slightly) invalid graph that mixes Tagged and Compressed
  // representations.
  Run<DecompressionOptimizationPhase>();

  auto call_descriptor = linkage->GetIncomingDescriptor();
  auto& compilation_data = data_provider_->GetDataComponent<CompilationData>();
  data_provider_->InitializeDataComponent<CodegenData>(
      &compilation_data.zone_stats, compilation_data.info, call_descriptor,
      assembler_options);

  // Select and schedule instructions covering the scheduled graph.
  CodeTracer* code_tracer = nullptr;
  if (compilation_data.info->trace_turbo_graph()) {
    // NOTE: We must not call `GetCodeTracer` if tracing is not enabled,
    // because it may not yet be initialized then and doing so from the
    // background thread is not threadsafe.
    UNIMPLEMENTED();
    // code_tracer = turbofan_data->GetCodeTracer();
  }
  if (base::Optional<BailoutReason> bailout = Run<InstructionSelectionPhase>(
          call_descriptor, linkage, code_tracer)) {
    compilation_data.info->AbortOptimization(*bailout);
    UNIMPLEMENTED();
    // turbofan_data->EndPhaseKind();
    // return false;
  }

  return true;
}

bool BuiltinPipeline::RunRegisterAllocation(CallDescriptor* call_descriptor) {
  InstructionSequenceData& instruction_data = data_provider_->GetDataComponent<InstructionSequenceData>();
//  PipelineData* data = this->data_;
  DCHECK_NOT_NULL(instruction_data.sequence);

  // TODO:
  // data->BeginPhaseKind("V8.TFRegisterAllocation");

  bool run_verifier = v8_flags.turbo_verify_allocation;

  // Allocate registers.

  const RegisterConfiguration* config = RegisterConfiguration::Default();
  std::unique_ptr<const RegisterConfiguration> restricted_config;
  if (call_descriptor->HasRestrictedAllocatableRegisters()) {
    RegList registers = call_descriptor->AllocatableRegisters();
    DCHECK_LT(0, registers.Count());
    restricted_config.reset(
        RegisterConfiguration::RestrictGeneralRegisters(registers));
    config = restricted_config.get();
  }
  AllocateRegisters(config, call_descriptor, run_verifier);

  // Verify the instruction sequence has the same hash in two stages.
  // TODO: VerifyGeneratedCodeIsIdempotent();

  Run<FrameElisionPhase>(false);

  // TODO(mtrofin): move this off to the register allocator.
  bool generate_frame_at_start =
      instruction_data.sequence->instruction_blocks().front()->must_construct_frame();
  // Optimimize jumps.
  if (v8_flags.turbo_jt) {
    Run<JumpThreadingPhase>(generate_frame_at_start);
  }

  // TODO: data->EndPhaseKind();

  return true;
}

void BuiltinPipeline::AllocateRegisters(const RegisterConfiguration* config,
                                        CallDescriptor* call_descriptor,
                                        bool run_verifier) {
//  PipelineData* data = this->data_;
  CompilationData& compilation_data =
      data_provider_->GetDataComponent<CompilationData>();
  CodegenData& codegen_data = data_provider_->GetDataComponent<CodegenData>();
  InstructionSequenceData& instruction_data =
      data_provider_->GetDataComponent<InstructionSequenceData>();
  OptimizedCompilationInfo* info = compilation_data.info;
  Isolate* isolate = data_provider_->GetDataComponent<ContextualData>().isolate;

  // Don't track usage for this zone in compiler stats.
  std::unique_ptr<Zone> verifier_zone;
  RegisterAllocatorVerifier* verifier = nullptr;
  if (run_verifier) {
        verifier_zone.reset(
            new Zone(codegen_data.codegen_zone->allocator(), kRegisterAllocatorVerifierZoneName));
        verifier = verifier_zone->New<RegisterAllocatorVerifier>(
            verifier_zone.get(), config, instruction_data.sequence, codegen_data.frame);
  }

#ifdef DEBUG
  instruction_data.sequence->ValidateEdgeSplitForm();
  instruction_data.sequence->ValidateDeferredBlockEntryPaths();
  instruction_data.sequence->ValidateDeferredBlockExitPaths();
#endif

  RegisterAllocatorData& ra_data =
      data_provider_->InitializeDataComponent<RegisterAllocatorData>(
          &compilation_data.zone_stats, config, codegen_data.frame,
          instruction_data.sequence, info);

  Run<MeetRegisterConstraintsPhase>();
  Run<ResolvePhisPhase>();
  Run<BuildLiveRangesPhase>();
  Run<BuildBundlesPhase>();

  // TODO:
  // TraceSequence(compilation_data.info, data, "before register allocation");
  if (verifier != nullptr) {
    CHECK(!ra_data.register_allocation_data->ExistsUseWithoutDefinition());
    CHECK(ra_data.register_allocation_data
    ->RangesDefinedInDeferredStayInDeferred());
  }

  if (info->trace_turbo_json() &&
      (false)) {  // TODO: && !data->MayHaveUnverifiableGraph()) {
    TurboCfgFile tcf(isolate);
    tcf << AsC1VRegisterAllocationData("PreAllocation",
                                       ra_data.register_allocation_data);
  }

  Run<AllocateGeneralRegistersPhase<LinearScanAllocator>>();

  if (instruction_data.sequence->HasFPVirtualRegisters()) {
    Run<AllocateFPRegistersPhase<LinearScanAllocator>>();
  }

  if (instruction_data.sequence->HasSimd128VirtualRegisters() &&
      (kFPAliasing == AliasingKind::kIndependent)) {
    Run<AllocateSimd128RegistersPhase<LinearScanAllocator>>();
  }

  Run<DecideSpillingModePhase>();
  Run<AssignSpillSlotsPhase>();
  Run<CommitAssignmentPhase>();

  // TODO(chromium:725559): remove this check once
  // we understand the cause of the bug. We keep just the
  // check at the end of the allocation.
  if (verifier != nullptr) {
    verifier->VerifyAssignment("Immediately after CommitAssignmentPhase.");
  }

  Run<ConnectRangesPhase>();

  Run<ResolveControlFlowPhase>();

  Run<PopulateReferenceMapsPhase>();

  if (v8_flags.turbo_move_optimization) {
    Run<OptimizeMovesPhase>();
  }

  // TODO:
  // TraceSequence(info, data, "after register allocation");

  if (verifier != nullptr) {
    verifier->VerifyAssignment("End of regalloc pipeline.");
    verifier->VerifyGapMoves();
  }

  if (info->trace_turbo_json() && (false)) { // TODO: && !data->MayHaveUnverifiableGraph()) {
    TurboCfgFile tcf(isolate);
    tcf << AsC1VRegisterAllocationData("CodeGen",
                                       ra_data.register_allocation_data);
  }

  data_provider_->ReleaseDataComponent<RegisterAllocatorData>();
}

void BuiltinPipeline::AssembleCode(Linkage* linkage) {
  // TODO:
  JumpOptimizationInfo* jump_optimization_info = nullptr;

  // TODO:
  // PipelineData* data = this->data_;
  // data->BeginPhaseKind("V8.TFCodeGeneration");
  ContextualData& contextual_data = data_provider_->GetDataComponent<ContextualData>();
  CompilationData& compilation_data = data_provider_->GetDataComponent<CompilationData>();
  CodegenData& codegen_data = data_provider_->GetDataComponent<CodegenData>();
  InstructionSequenceData& instruction_data = data_provider_->GetDataComponent<InstructionSequenceData>();

  codegen_data.code_generator = ZoneWithNamePointer<CodeGenerator, kCodegenZoneName>(new CodeGenerator(codegen_data.codegen_zone,
    codegen_data.frame, linkage, instruction_data.sequence, compilation_data.info,
    contextual_data.isolate, codegen_data.osr_helper, compilation_data.start_source_position,
    jump_optimization_info,
    codegen_data.assembler_options, compilation_data.info->builtin(), instruction_data.max_unoptimized_frame_height,
    instruction_data.max_pushed_argument_count, v8_flags.trace_turbo_stack_accesses ?
    compilation_data.info->GetDebugName().get() : nullptr));

  UnparkedScopeIfNeeded unparked_scope(compilation_data.broker.get());

  Run<AssembleCodePhase>();
  if (compilation_data.info->trace_turbo_json()) {
    // TODO:
//    TurboJsonFile json_of(compilation_data.info, std::ios_base::app);
//    json_of << "{\"name\":\"code generation\""
//            << ", \"type\":\"instructions\""
//            << InstructionStartsAsJSON{&data->code_generator()->instr_starts()}
//            << TurbolizerCodeOffsetsInfoAsJSON{
//                   &data->code_generator()->offsets_info()};
//    json_of << "},\n";
  }
  //data->DeleteInstructionZone();
  // data_provider_->ReleaseDataComponent<CodegenData>();
  // TODO: data->EndPhaseKind();
}

MaybeHandle<Code> BuiltinPipeline::FinalizeCode(bool retire_broker) {
  // TODO:
  // PipelineData* data = this->data_;
  // data->BeginPhaseKind("V8.TFFinalizeCode");
  // ContextualData& contextual_data = data_provider_->GetDataComponent<ContextualData>();
  CompilationData& compilation_data = data_provider_->GetDataComponent<CompilationData>();
  OptimizedCompilationInfo* info = compilation_data.info;
  if(compilation_data.broker && retire_broker) {
    compilation_data.broker->Retire();
  }
  MaybeHandle<Code> maybe_code = Run<FinalizeCodePhase>();
  data_provider_->ReleaseDataComponent<CodegenData>();

  Handle<Code> code;
  if (!maybe_code.ToHandle(&code)) {
    return maybe_code;
  }

  info->SetCode(code);
  // TODO: PrintCode(contextual_data.isolate, code, info);

  // Functions with many inline candidates are sensitive to correct call
  // frequency feedback and should therefore not be tiered up early.
  if (v8_flags.profile_guided_optimization &&
      info->could_not_inline_all_candidates()) {
    info->shared_info()->set_cached_tiering_decision(
        CachedTieringDecision::kNormal);
  }

  if (info->trace_turbo_json()) {
    TurboJsonFile json_of(info, std::ios_base::app);
    UNIMPLEMENTED();
//    json_of << "{\"name\":\"disassembly\",\"type\":\"disassembly\""
//            << BlockStartsAsJSON{&data->code_generator()->block_starts()}
//            << "\"data\":\"";
//#ifdef ENABLE_DISASSEMBLER
//    std::stringstream disassembly_stream;
//    code->Disassemble(nullptr, disassembly_stream, isolate());
//    std::string disassembly_string(disassembly_stream.str());
//    for (const auto& c : disassembly_string) {
//      json_of << AsEscapedUC16ForJSON(c);
//    }
//#endif  // ENABLE_DISASSEMBLER
//    json_of << "\"}\n],\n";
//    json_of << "\"nodePositions\":";
//    // TODO(nicohartmann@): We should try to always provide source positions.
//    json_of << (data->source_position_output().empty()
//                    ? "{}"
//                    : data->source_position_output())
//            << ",\n";
//    JsonPrintAllSourceWithPositions(json_of, data->info(), isolate());
//    if (info()->has_bytecode_array()) {
//      json_of << ",\n";
//      JsonPrintAllBytecodeSources(json_of, info());
//    }
//    json_of << "\n}";
  }
  if (info->trace_turbo_json() || info->trace_turbo_graph()) {
    UNIMPLEMENTED();
//    CodeTracer::StreamScope tracing_scope(data->GetCodeTracer());
//    tracing_scope.stream()
//        << "---------------------------------------------------\n"
//        << "Finished compiling method " << info()->GetDebugName().get()
//        << " using TurboFan" << std::endl;
  }
  // TODO: data->EndPhaseKind();
  return code;
}

}  // namespace v8::internal::compiler::turboshaft
