// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_PIPELINES_H_
#define V8_COMPILER_TURBOSHAFT_PIPELINES_H_

#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/pipeline-statistics.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/logging/runtime-call-stats.h"
#include "src/zone/accounting-allocator.h"

namespace v8::internal::compiler::turboshaft {

static constexpr char kGraphZoneName[] = "graph-zone";

#if 0
class PipelineData2 {

  const TurboshaftPipelineKind pipeline_kind;
  OptimizedCompilationInfo* const info;
  Schedule* turbofan_schedule;
  Zone* graph_zone; // What's the meaning of this?
  Zone* zone; // and this?
  std::unique_ptr<JSHeapBroker> broker;
  Isolate* const isolate;
  SourcePositionTable* source_positions;
};
#endif

enum class DataComponentKind {
  kContextualData,
  kCompilationData,
  kGraphData,
  kStatisticsData,
};

#define DEFINE_DATA_COMPONENT(name) \
  static constexpr DataComponentKind kKind = DataComponentKind::k##name;

// `ContextualData` is persistent throughout the entire pipeline, but it's owned
// and provided by the surrounding context.
struct ContextualData {
  DEFINE_DATA_COMPONENT(ContextualData)

  Isolate* isolate;

  explicit ContextualData(Isolate* isolate) : isolate(isolate) {}
};

// `CompilationData` persists throughout the entire pipeline and is owned by the
// pipeline.
struct CompilationData {
  DEFINE_DATA_COMPONENT(CompilationData)

  OptimizedCompilationInfo info;
  TurboshaftPipelineKind pipeline_kind;
  ZoneStats zone_stats;
  //  NodeOriginTable node_origins;

  CompilationData(CodeKind code_kind, const char* debug_name, Zone* graph_zone,
                  TurboshaftPipelineKind pipeline_kind,
                  AccountingAllocator* allocator)
      : info(base::CStrVector(debug_name), graph_zone, code_kind),
        pipeline_kind(pipeline_kind),
        zone_stats(allocator) {}
};

struct GraphData {
  DEFINE_DATA_COMPONENT(GraphData)

  ZoneStats::Scope graph_zone_scope;
  Zone* graph_zone;
  Graph* graph;

  explicit GraphData(ZoneStats* zone_stats)
      : graph_zone_scope(zone_stats, kGraphZoneName, kCompressGraphZone),
        graph_zone(graph_zone_scope.zone()),
        graph(graph_zone->New<Graph>(graph_zone)) {}
};

struct StatisticsData {
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
  explicit DataComponentProvider(Zone* zone) : zone_(zone) {}

  template <typename Component>
  bool HasDataComponent() const {
    const size_t kind = static_cast<size_t>(Component::kKind);
    if (kind >= components_.size()) return false;
    return components_[kind] != nullptr;
  }

  template <typename Component>
  const Component& GetDataComponent() const {
    const size_t kind = static_cast<size_t>(Component::kKind);
    DCHECK_LT(kind, components_.size());
    DCHECK_NOT_NULL(components_[kind]);
    return *reinterpret_cast<Component*>(components_[kind]);
  }

  template <typename Component>
  Component& GetDataComponent() {
    const size_t kind = static_cast<size_t>(Component::kKind);
    DCHECK_LT(kind, components_.size());
    DCHECK_NOT_NULL(components_[kind]);
    return *reinterpret_cast<Component*>(components_[kind]);
  }

  template <typename Component, typename... Args>
  Component& InitializeDataComponent(Args&&... args) {
    const size_t kind = static_cast<size_t>(Component::kKind);
    if (kind >= components_.size()) {
      components_.resize(kind + 1);
    }
    DCHECK_NULL(components_[kind]);
    components_[kind] = zone_->New<Component>(std::forward<Args>(args)...);
    return *reinterpret_cast<Component*>(components_[kind]);
  }

 private:
  std::vector<void*> components_;
  Zone* zone_;
};

class PipelineBase : public DataComponentProvider {
 public:
  // TODO
  struct MiscData {
    RuntimeCallStats* runtime_call_stats;
  };

  StatisticsData* CreateStatistics(NodeOriginTable* node_origins = nullptr) {
    DCHECK(!HasDataComponent<StatisticsData>());
    ContextualData& contextual_data = GetDataComponent<ContextualData>();
    CompilationData& compilation_data = GetDataComponent<CompilationData>();
    return &InitializeDataComponent<StatisticsData>(
        &compilation_data.info, contextual_data.isolate->GetTurboStatistics(),
        &compilation_data.zone_stats, node_origins);
  }

  const ContextualData* contextual_data() const {
    return &GetDataComponent<ContextualData>();
  }
  CompilationData* compilation_data() {
    return &GetDataComponent<CompilationData>();
  }
  RuntimeCallStats* runtime_call_stats() const { return runtime_call_stats_; }
  void set_runtime_call_stats(RuntimeCallStats* stats) {
    runtime_call_stats_ = stats;
  }

  base::Optional<BailoutReason> InitializeFromTurbofanGraph(
      Schedule* turbofan_graph, CallDescriptor* call_descriptor);

 protected:
  PipelineBase(ContextualData contextual_data, CodeKind code_kind,
               const char* debug_name, Zone* graph_zone,
               TurboshaftPipelineKind pipeline_kind)
      // TODO(nicohartmann@): This needs a permanent zone.
      : DataComponentProvider(graph_zone) {
    Isolate* isolate = contextual_data.isolate;
    InitializeDataComponent<ContextualData>(std::move(contextual_data));
    auto& compilation_data = InitializeDataComponent<CompilationData>(
        code_kind, debug_name, graph_zone, pipeline_kind, isolate->allocator());
    InitializeDataComponent<GraphData>(&compilation_data.zone_stats);
  }

  template <typename Phase, typename... Args>
  auto Run(Args&&... args) {
    static_assert(Phase::kKind == PhaseKind::kTurboshaft);
    CompilationData& compilation_data = GetDataComponent<CompilationData>();

    // TODO
    TurbofanPipelineStatistics* pipeline_statistics = nullptr;
    NodeOriginTable* node_origins = nullptr;
    if (HasDataComponent<StatisticsData>()) {
      StatisticsData& statistics_data = GetDataComponent<StatisticsData>();
      pipeline_statistics = &statistics_data.pipeline_statistics;
      node_origins = statistics_data.node_origins;
    }
#ifdef V8_RUNTIME_CALL_STATS
    PipelineRunScope scope(pipeline_statistics, &compilation_data.zone_stats,
                           node_origins, runtime_call_stats_,
                           Phase::phase_name(), Phase::kRuntimeCallCounterId,
                           Phase::kCounterMode);
#else
    PipelineRunScope scope(pipeline_statistics, &compilation_data.zone_stats,
                           node_origins, runtime_call_stats_,
                           Phase::phase_name());
#endif

    Phase phase;
    using result_t =
        decltype(phase.Run(this, scope.zone(), std::forward<Args>(args)...));
    CodeTracer* code_tracer = nullptr;
    USE(code_tracer);
    if (compilation_data.info.trace_turbo_graph()) {
      // NOTE: We must not call `GetCodeTracer` if tracing is not enabled,
      // because it may not yet be initialized then and doing so from the
      // background thread is not threadsafe.
      //      code_tracer = this->data_->GetCodeTracer();
      UNIMPLEMENTED();
    }
    if constexpr (std::is_same_v<result_t, void>) {
      phase.Run(this, scope.zone(), std::forward<Args>(args)...);
      if constexpr (Phase::outputs_printable_graph) {
        PrintTurboshaftGraph(scope.zone(), code_tracer, Phase::phase_name());
      }
      return;
    } else {
      auto result = phase.Run(this, scope.zone(), std::forward<Args>(args)...);
      if constexpr (Phase::outputs_printable_graph) {
        PrintTurboshaftGraph(scope.zone(), code_tracer, Phase::phase_name());
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
};

class BuiltinPipeline : public PipelineBase {
 public:
  BuiltinPipeline(ContextualData contextual_data, CodeKind code_kind,
                  const char* debug_name, Zone* graph_zone, Builtin builtin)
      : PipelineBase(std::move(contextual_data), code_kind, debug_name,
                     graph_zone, TurboshaftPipelineKind::kCSA) {
    GetDataComponent<CompilationData>().info.set_builtin(builtin);
  }

  void SetInputGraph(Graph* graph) {
    UNIMPLEMENTED();
    //    InitializeDataComponent<GraphData>(graph, graph->graph_zone());
  }

  void RunOptimizations();
  void RunInstructionSelection();

 private:
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_PIPELINES_H_
