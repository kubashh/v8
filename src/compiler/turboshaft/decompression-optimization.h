// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_DECOMPRESSION_OPTIMIZATION_H_
#define V8_COMPILER_TURBOSHAFT_DECOMPRESSION_OPTIMIZATION_H_

namespace v8 {
namespace internal {

class Zone;
namespace compiler {
namespace turboshaft {

class Graph;

void RunDecompressionOptimization(Graph& graph, Zone* phase_zone);

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_DECOMPRESSION_OPTIMIZATION_H_
