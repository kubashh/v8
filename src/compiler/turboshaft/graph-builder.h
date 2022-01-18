// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_GRAPH_BUILDER_H_
#define V8_COMPILER_TURBOSHAFT_GRAPH_BUILDER_H_

#include "src/compiler/turboshaft/cfg.h"

namespace v8 {
namespace internal {
namespace compiler {

class Schedule;

namespace turboshaft {

Graph BuildGraph(Schedule* schedule, Zone* graph_zone, Zone* temp_zone);

}
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_GRAPH_BUILDER_H_
