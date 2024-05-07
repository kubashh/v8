// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/loop-unrolling-phase.h"

#include "src/base/logging.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/loop-unrolling-reducer.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "src/compiler/turboshaft/value-numbering-reducer.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "src/numbers/conversions-inl.h"

namespace v8::internal::compiler::turboshaft {

void LoopUnrollingPhase::Run(PipelineData* data, Zone* temp_zone) {
  LoopUnrollingAnalyzer analyzer(temp_zone, &data->graph(), data->is_wasm());
  if (analyzer.CanUnrollAtLeastOneLoop()) {
    data->graph().set_loop_unrolling_analyzer(&analyzer);
    turboshaft::CopyingPhase<LoopStackCheckElisionReducer, LoopUnrollingReducer,
                             MachineOptimizationReducer,
                             ValueNumberingReducer>::Run(data, temp_zone);
    DCHECK(!data->graph().has_loop_unrolling_analyzer());
    DCHECK(!data->graph().GetOrCreateCompanion().has_loop_unrolling_analyzer());
    // data->graph().clear_loop_unrolling_analyzer();
  }
}

}  // namespace v8::internal::compiler::turboshaft
