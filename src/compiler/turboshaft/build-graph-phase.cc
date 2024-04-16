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
    Linkage* linkage) {
  DCHECK_NOT_NULL(schedule);
  if (auto bailout =
          turboshaft::BuildGraph(data_provider, schedule, temp_zone, linkage)) {
    return bailout;
  }
  return {};
}

base::Optional<BailoutReason> BuildGraphPhase::Run(Zone* temp_zone,
                                                   Linkage* linkage) {
  Schedule* schedule = PipelineData::Get().schedule();
  PipelineData::Get().reset_schedule();
  return Run(nullptr, temp_zone, schedule, linkage);
}

}  // namespace v8::internal::compiler::turboshaft
