// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/decompression-optimization-phase.h"

#include "src/compiler/turboshaft/decompression-optimization.h"
#include "src/compiler/turboshaft/pipelines.h"

namespace v8::internal::compiler::turboshaft {

void DecompressionOptimizationPhase::Run(Zone* temp_zone) {
  Run(nullptr, temp_zone);
}

void DecompressionOptimizationPhase::Run(DataComponentProvider* data_provider, Zone* temp_zone) {
  if (!COMPRESS_POINTERS_BOOL) return;

  Graph& graph = data_provider ? *data_provider->GetDataComponent<GraphData>().graph : PipelineData::Get().graph();
  turboshaft::RunDecompressionOptimization(graph, temp_zone);
}

}  // namespace v8::internal::compiler::turboshaft
