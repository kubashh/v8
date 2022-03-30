// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_RECREATE_SCHEDULE_H_
#define V8_COMPILER_TURBOSHAFT_RECREATE_SCHEDULE_H_

namespace v8 {
namespace internal {
class Zone;
namespace compiler {
class Schedule;
class Graph;
class CallDescriptor;
namespace turboshaft {
class Graph;

struct RecreateScheduleResult {
  compiler::Graph* graph;
  Schedule* schedule;
};

RecreateScheduleResult RecreateSchedule(const Graph& graph,
                                        CallDescriptor* call_descriptor,
                                        Zone* zone);

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8
#endif  // V8_COMPILER_TURBOSHAFT_RECREATE_SCHEDULE_H_
