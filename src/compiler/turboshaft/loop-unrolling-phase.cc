// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/loop-unrolling-phase.h"

#include "src/compiler/turboshaft/loop-unrolling-reducer.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/optimization-phase.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "src/compiler/turboshaft/value-numbering-reducer.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "src/numbers/conversions-inl.h"

namespace v8::internal::compiler::turboshaft {

void LoopUnrollingPhase::Run(Zone* temp_zone) {
  // Do we need this for LoopUnrollingReducer? I think currently we need it for
  // detecting Stack checks (and probably it's also needed for
  // LateLoadElimination but is missing over there).
  //
  // UnparkedScopeIfNeeded scope(PipelineData::Get().broker());
  // AllowHandleDereference allow_handle_dereference;

  turboshaft::OptimizationPhase<
      turboshaft::LoopUnrollingReducer, turboshaft::VariableReducer,
      //turboshaft::MachineOptimizationReducerSignallingNanImpossible,
      turboshaft::RequiredOptimizationReducer,
      turboshaft::ValueNumberingReducer>::Run(temp_zone);
}

}  // namespace v8::internal::compiler::turboshaft
