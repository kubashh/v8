// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PHASE_H_
#define V8_COMPILER_PHASE_H_

#include "src/compiler/node-origin-table.h"
#include "src/compiler/pipeline-statistics.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/logging/runtime-call-stats.h"

#ifdef V8_RUNTIME_CALL_STATS
#define DECL_PIPELINE_PHASE_CONSTANTS_HELPER(Name, Kind, Mode)  \
  static constexpr PhaseKind kKind = Kind;                      \
  static const char* phase_name() { return "V8.TF" #Name; }     \
  static constexpr RuntimeCallCounterId kRuntimeCallCounterId = \
      RuntimeCallCounterId::kOptimize##Name;                    \
  static constexpr RuntimeCallStats::CounterMode kCounterMode = Mode;
#else  // V8_RUNTIME_CALL_STATS
#define DECL_PIPELINE_PHASE_CONSTANTS_HELPER(Name, Kind, Mode) \
  static constexpr PhaseKind kKind = Kind;                     \
  static const char* phase_name() { return "V8.TF" #Name; }
#endif  // V8_RUNTIME_CALL_STATS

#define DECL_PIPELINE_PHASE_CONSTANTS(Name)                        \
  DECL_PIPELINE_PHASE_CONSTANTS_HELPER(Name, PhaseKind::kTurbofan, \
                                       RuntimeCallStats::kThreadSpecific)

#define DECL_MAIN_THREAD_PIPELINE_PHASE_CONSTANTS(Name)            \
  DECL_PIPELINE_PHASE_CONSTANTS_HELPER(Name, PhaseKind::kTurbofan, \
                                       RuntimeCallStats::kExact)

namespace v8::internal::compiler {

enum class PhaseKind {
  kTurbofan,
  kTurboshaft,
};

class NodeOriginTable;
class TurbofanPipelineStatistics;
class ZoneStats;

class V8_NODISCARD PipelineRunScope {
 public:
#ifdef V8_RUNTIME_CALL_STATS
  //  PipelineRunScope(
  //      PipelineData* data, const char* phase_name,
  //      RuntimeCallCounterId runtime_call_counter_id,
  //      RuntimeCallStats::CounterMode counter_mode = RuntimeCallStats::kExact)
  //      : phase_scope_(data->pipeline_statistics(), phase_name),
  //        zone_scope_(data->zone_stats(), phase_name),
  //        origin_scope_(data->node_origins(), phase_name),
  //        runtime_call_timer_scope(data->runtime_call_stats(),
  //                                 runtime_call_counter_id, counter_mode) {
  //    DCHECK_NOT_NULL(phase_name);
  //  }
  PipelineRunScope(
      TurbofanPipelineStatistics* pipeline_statistics, ZoneStats* zone_stats,
      NodeOriginTable* node_origins, RuntimeCallStats* runtime_call_stats,
      const char* phase_name, RuntimeCallCounterId runtime_call_counter_id,
      RuntimeCallStats::CounterMode counter_mode = RuntimeCallStats::kExact)
      : phase_scope_(pipeline_statistics, phase_name),
        zone_scope_(zone_stats, phase_name),
        origin_scope_(node_origins, phase_name),
        runtime_call_timer_scope(runtime_call_stats, runtime_call_counter_id,
                                 counter_mode) {
    DCHECK_NOT_NULL(phase_name);
  }
#else   // V8_RUNTIME_CALL_STATS
  //  PipelineRunScope(PipelineData* data, const char* phase_name)
  //      : phase_scope_(data->pipeline_statistics(), phase_name),
  //        zone_scope_(data->zone_stats(), phase_name),
  //        origin_scope_(data->node_origins(), phase_name) {
  //    DCHECK_NOT_NULL(phase_name);
  //  }
  PipelineRunScope(TurbofanPipelineStatistics* pipeline_statistics,
                   ZoneStats* zone_stats, NodeOriginTable* node_origins,
                   RuntimeCallStats* runtime_call_stats, const char* phase_name)
      : phase_scope_(pipeline_statistics, phase_name),
        zone_scope_(zone_stats, phase_name),
        origin_scope_(node_origins, phase_name) {
    DCHECK_NOT_NULL(phase_name);
  }
#endif  // V8_RUNTIME_CALL_STATS

  Zone* zone() { return zone_scope_.zone(); }

 private:
  PhaseScope phase_scope_;
  ZoneStats::Scope zone_scope_;
  NodeOriginTable::PhaseScope origin_scope_;
#ifdef V8_RUNTIME_CALL_STATS
  RuntimeCallTimerScope runtime_call_timer_scope;
#endif  // V8_RUNTIME_CALL_STATS
};

}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_PHASE_H_
