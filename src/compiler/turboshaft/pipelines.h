// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_PIPELINES_H_
#define V8_COMPILER_TURBOSHAFT_PIPELINES_H_

#include "src/codegen/optimized-compilation-info.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/backend/register-allocator.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/osr.h"
#include "src/compiler/phase.h"
#include "src/compiler/pipeline-statistics.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/logging/runtime-call-stats.h"
#include "src/zone/accounting-allocator.h"
#include "src/compiler/backend/code-generator.h"

namespace v8::internal::compiler::turboshaft {

static constexpr char kGraphZoneName[] = "graph-zone";

enum class DataComponentKind {
  kContextualData,
  kCompilationData,
  kGraphData,
  kCodegenData,
  kInstructionSequenceData,
  kRegisterAllocationData,
  kStatisticsData,
  // This has to be the last value.
  kComponentCount,
};

struct DataComponent {};

#define DEFINE_DATA_COMPONENT(name) \
  static constexpr DataComponentKind kKind = DataComponentKind::k##name;

// `ContextualData` is persistent throughout the entire pipeline, but it's owned
// and provided by the surrounding context.
struct ContextualData : DataComponent {
  DEFINE_DATA_COMPONENT(ContextualData)

  Isolate* isolate;

  explicit ContextualData(Isolate* isolate) : isolate(isolate) {}
};

// `CompilationData` persists throughout the entire pipeline and is owned by the
// pipeline.
struct CompilationData : DataComponent {
  DEFINE_DATA_COMPONENT(CompilationData)

  OptimizedCompilationInfo* info;
  ZoneStats zone_stats;
  std::unique_ptr<JSHeapBroker> broker;
  // TODO(nicohartmann@): `pipeline_kind` should be somewhere else.
  TurboshaftPipelineKind pipeline_kind;
  int start_source_position = kNoSourcePosition;
  CodeTracer* code_tracer = nullptr;
  //  NodeOriginTable node_origins;

  CompilationData(OptimizedCompilationInfo* info,
                  std::unique_ptr<JSHeapBroker> broker,
                  TurboshaftPipelineKind pipeline_kind,
                  AccountingAllocator* allocator)
      : info(info),
        zone_stats(allocator),
        broker(std::move(broker)),
        pipeline_kind(pipeline_kind) {}
};

struct GraphData : DataComponent {
  DEFINE_DATA_COMPONENT(GraphData)

  ZoneWithName<kGraphZoneName> graph_zone;
  //  ZoneStats::Scope graph_zone_scope;
  //  Zone* graph_zone;
  //  Graph* graph;
  // Technically, in some instances of `GraphData`, (some of) the following
  // pointers might not actually point into the graph zone, but may be provided
  // from outside. However, we consider these pointers valid only as long as the
  // graph zone is alive.
  ZoneWithNamePointer<Graph, kGraphZoneName> graph;
  //  ZoneWithNamePointer<SourcePositionTable, kGraphZoneName> source_positions
  //  = nullptr;
  ZoneWithNamePointer<NodeOriginTable, kGraphZoneName> node_origins = nullptr;
  //    source_positions_ = nullptr;
  //    node_origins_ = nullptr;
  //    simplified_ = nullptr;
  //    machine_ = nullptr;
  //    common_ = nullptr;
  //    javascript_ = nullptr;
  //    jsgraph_ = nullptr;
  //    mcgraph_ = nullptr;
  //    schedule_ = nullptr;

  explicit GraphData(ZoneStats* zone_stats, NodeOriginTable* node_origins)
      : graph_zone(zone_stats, kGraphZoneName, kCompressGraphZone),
        graph(graph_zone.New<Graph>(graph_zone)),
        node_origins(node_origins) {
  }  // ZoneWithNamePointer<NodeOriginTable, kGraphZoneName>(node_origins)) {}
  //      : graph_zone_scope(zone_stats, kGraphZoneName, kCompressGraphZone),
  //        graph_zone(graph_zone_scope.zone()),
  //        graph(graph_zone->New<Graph>(graph_zone)) {}

  //  void AllocateAll() {
  //    DCHECK_NULL(graph);
  //    DCHECK_NULL(source_positions);
  //    graph = graph_zone.New<Graph>(&graph_zone);
  //    source_positions = graph_zone.New<SourcePositionTable>(graph);
  //  }
};

struct CodegenData : DataComponent {
  DEFINE_DATA_COMPONENT(CodegenData)

  ZoneWithName<kCodegenZoneName> codegen_zone;
  ZoneWithNamePointer<Frame, kCodegenZoneName> frame = nullptr;
  ZoneWithNamePointer<CodeGenerator, kCodegenZoneName> code_generator = nullptr;
  base::Optional<OsrHelper> osr_helper;
  AssemblerOptions assembler_options;
  bool has_special_rpo = false;
  MaybeHandle<Code> code;

  CodegenData(ZoneStats* zone_stats, OptimizedCompilationInfo* info,
              CallDescriptor* call_descriptor,
              const AssemblerOptions assembler_options)
      : codegen_zone(zone_stats, kCodegenZoneName),
        assembler_options(assembler_options) {
    DCHECK_NOT_NULL(call_descriptor);
    int fixed_frame_size =
        call_descriptor->CalculateFixedFrameSize(info->code_kind());
    frame = codegen_zone.New<Frame>(fixed_frame_size, codegen_zone);
    if (info->is_osr()) {
      osr_helper.emplace(info);
      osr_helper->SetupFrame(frame);
    }
  }
};

struct InstructionSequenceData : DataComponent {
  DEFINE_DATA_COMPONENT(InstructionSequenceData)

  ZoneWithName<kInstructionZoneName> instruction_zone;
  ZoneWithNamePointer<InstructionBlocks, kInstructionZoneName> blocks;
  ZoneWithNamePointer<InstructionSequence, kInstructionZoneName> sequence;
  size_t max_unoptimized_frame_height = 0;
  size_t max_pushed_argument_count = 0;

  InstructionSequenceData(ZoneStats* zone_stats, Isolate* isolate,
                          const Graph* graph,
                          const CallDescriptor* call_descriptor)
      : instruction_zone(zone_stats, kInstructionZoneName) {
    blocks = ZoneWithNamePointer<InstructionBlocks, kInstructionZoneName>(
        InstructionSequence::InstructionBlocksFor(instruction_zone, *graph));
    sequence = instruction_zone.New<InstructionSequence>(
        isolate, instruction_zone, blocks);
    if (call_descriptor && call_descriptor->RequiresFrameAsIncoming()) {
      sequence->instruction_blocks()[0]->mark_needs_frame();
    } else {
      DCHECK(call_descriptor->CalleeSavedFPRegisters().is_empty());
    }
  }
};

struct RegisterAllocatorData : DataComponent {
  DEFINE_DATA_COMPONENT(RegisterAllocationData)

  ZoneWithName<kRegisterAllocationZoneName> register_allocation_zone;
  ZoneWithNamePointer<RegisterAllocationData, kRegisterAllocationZoneName>
      register_allocation_data;

  explicit RegisterAllocatorData(ZoneStats* zone_stats,
                                 const RegisterConfiguration* config,
                                 Frame* frame, InstructionSequence* sequence,
                                 OptimizedCompilationInfo* info)
      : register_allocation_zone(zone_stats, kRegisterAllocationZoneName) {
    register_allocation_data =
        register_allocation_zone.New<RegisterAllocationData>(
            config, register_allocation_zone, frame, sequence,
            &info->tick_counter(), info->GetDebugName().get());
  }
};

struct StatisticsData : DataComponent {
  DEFINE_DATA_COMPONENT(StatisticsData)

  TurbofanPipelineStatistics pipeline_statistics;
  NodeOriginTable* node_origins;

  StatisticsData(OptimizedCompilationInfo* info,
                 std::shared_ptr<CompilationStatistics> compilation_statistics,
                 ZoneStats* zone_stats, NodeOriginTable* node_origins)
      : pipeline_statistics(info, compilation_statistics, zone_stats),
        node_origins(node_origins) {}
};

class DataComponentProvider {
 public:
  DataComponentProvider()
      : components_(static_cast<size_t>(DataComponentKind::kComponentCount)) {}

  template <typename Component>
  bool HasDataComponent() const {
    const size_t kind = static_cast<size_t>(Component::kKind);
    return components_[kind] != nullptr;
  }

  template <typename Component>
  const Component& GetDataComponent() const {
    const size_t kind = static_cast<size_t>(Component::kKind);
    DCHECK_NOT_NULL(components_[kind]);
    return *static_cast<Component*>(components_[kind].get());
  }

  template <typename Component>
  Component& GetDataComponent() {
    const size_t kind = static_cast<size_t>(Component::kKind);
    DCHECK_NOT_NULL(components_[kind]);
    return *static_cast<Component*>(components_[kind].get());
  }

  template <typename Component>
  Component& InitializeDataComponent(std::unique_ptr<Component> component) {
    DCHECK_NOT_NULL(component);
    const size_t kind = static_cast<size_t>(Component::kKind);
    DCHECK_NULL(components_[kind]);
    components_[kind] = std::move(component);
    return *static_cast<Component*>(components_[kind].get());
  }

  template <typename Component, typename... Args>
  Component& InitializeDataComponent(Args&&... args) {
    const size_t kind = static_cast<size_t>(Component::kKind);
    DCHECK_NULL(components_[kind]);
    components_[kind] =
        std::make_unique<Component>(std::forward<Args>(args)...);
    return *static_cast<Component*>(components_[kind].get());
  }

  template<typename Component>
  std::unique_ptr<Component> ReleaseDataComponent() {
    const size_t kind = static_cast<size_t>(Component::kKind);
    DCHECK_NOT_NULL(components_[kind]);
    return std::unique_ptr<Component>(static_cast<Component*>(components_[kind].release()));
  }

 private:
  std::vector<std::unique_ptr<DataComponent>> components_;
};

class PipelineBase {
 public:
  // TODO
  struct MiscData {
    RuntimeCallStats* runtime_call_stats;
  };

protected:
  explicit PipelineBase(DataComponentProvider* data_provider)
      : data_provider_(data_provider) {}

  template <typename Phase, typename... Args>
  auto Run(Args&&... args) {
    //    static_assert(Phase::kKind == PhaseKind::kTurboshaft);
    CompilationData& compilation_data =
        data_provider_->GetDataComponent<CompilationData>();

    // TODO
    TurbofanPipelineStatistics* pipeline_statistics = nullptr;
    NodeOriginTable* node_origins = nullptr;
    if (data_provider_->HasDataComponent<StatisticsData>()) {
      StatisticsData& statistics_data =
          data_provider_->GetDataComponent<StatisticsData>();
      pipeline_statistics = &statistics_data.pipeline_statistics;
      node_origins = statistics_data.node_origins;
    }
    PipelineRunScope scope(pipeline_statistics, &compilation_data.zone_stats,
                           node_origins, runtime_call_stats_,
                           Phase::phase_name()
#ifdef V8_RUNTIME_CALL_STATS
                               ,
                           Phase::kRuntimeCallCounterId, Phase::kCounterMode
#endif
    );

    Phase phase;
    using result_t = decltype(phase.Run(data_provider_, scope.zone(),
                                        std::forward<Args>(args)...));
    CodeTracer* code_tracer = nullptr;
    USE(code_tracer);
    if (compilation_data.info->trace_turbo_graph()) {
      // NOTE: We must not call `GetCodeTracer` if tracing is not enabled,
      // because it may not yet be initialized then and doing so from the
      // background thread is not threadsafe.
      code_tracer = compilation_data.code_tracer;
      DCHECK_NOT_NULL(code_tracer);
    }
    if constexpr (std::is_same_v<result_t, void>) {
      phase.Run(data_provider_, scope.zone(), std::forward<Args>(args)...);
      if constexpr (Phase::outputs_printable_graph) {
        PrintTurboshaftGraph(data_provider_, scope.zone(), code_tracer,
                             Phase::phase_name());
      }
      return;
    } else {
      auto result =
          phase.Run(data_provider_, scope.zone(), std::forward<Args>(args)...);
      if constexpr (Phase::outputs_printable_graph) {
        PrintTurboshaftGraph(data_provider_, scope.zone(), code_tracer,
                             Phase::phase_name());
      }
      return result;
    }
    UNREACHABLE();
  }

  // Data components.
  RuntimeCallStats* runtime_call_stats_ = nullptr;

  //    turboshaft::Tracing::Scope tracing_scope(&info_);
  //
  //    UnparkedScopeIfNeeded scope(data_.broker(),
  //                                v8_flags.turboshaft_trace_reduction);
  //    turboshaft::PipelineData::Scope turboshaft_scope(
  //        data_.GetTurboshaftPipelineData(
  //            turboshaft::TurboshaftPipelineKind::kCSA, graph));
  //

  DataComponentProvider* data_provider_;
};

class BuiltinPipeline : public PipelineBase {
 public:
  explicit BuiltinPipeline(DataComponentProvider* data_provider)
      : PipelineBase(data_provider) {}

  base::Optional<BailoutReason> RunGraphConstructonFromTurbofan(
      Schedule* schedule, SourcePositionTable* source_positions,
      NodeOriginTable* node_origins, Linkage* linkage);

  void RunOptimizations();
  bool RunInstructionSelection(Linkage* linkage,
                               const AssemblerOptions& assembler_options);
  bool RunRegisterAllocation(CallDescriptor* call_descriptor);
  void AssembleCode(Linkage* linkage);
  MaybeHandle<Code> FinalizeCode(bool retire_broker = true);

 private:
  void AllocateRegisters(const RegisterConfiguration* config,
                         CallDescriptor* call_descriptor, bool run_verifier);
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_PIPELINES_H_
