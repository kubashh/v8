// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/build-graph-phase.h"

#include "src/compiler/js-heap-broker.h"
#include "src/compiler/turboshaft/graph-builder.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/pipelines.h"

namespace v8::internal::compiler::turboshaft {

base::Optional<BailoutReason> BuildGraphPhase::Run(
    DataComponentProvider* data_provider, Zone* temp_zone, Schedule* schedule,
    SourcePositionTable* source_positions, NodeOriginTable* node_origins,
    Linkage* linkage) {
  DCHECK_NOT_NULL(schedule);
  // This phase constructs graph data.
  if(data_provider) {
    ZoneStats* zone_stats =
        &data_provider->GetDataComponent<CompilationData>().zone_stats;
    data_provider->InitializeDataComponent<GraphData>(zone_stats, node_origins);
  }
  if (auto bailout = turboshaft::BuildGraph(
          data_provider, schedule, source_positions, temp_zone, linkage)) {
    return bailout;
  }
  return {};
}

base::Optional<BailoutReason> BuildGraphPhase::Run(Zone* temp_zone,
                                                   Linkage* linkage) {
  Schedule* schedule = PipelineData::Get().schedule();
  PipelineData::Get().reset_schedule();
  return Run(nullptr, temp_zone, schedule, nullptr, nullptr, linkage);
}

}  // namespace v8::internal::compiler::turboshaft
