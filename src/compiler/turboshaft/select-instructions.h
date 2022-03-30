// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_SELECT_INSTRUCTIONS_H_
#define V8_COMPILER_TURBOSHAFT_SELECT_INSTRUCTIONS_H_

#include "src/compiler/backend/instruction-selector.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

class Graph;

bool SelectInstructions(
    Zone* zone, Linkage* linkage, InstructionSequence* sequence,
    const Graph& graph, SourcePositionTable* source_positions, Frame* frame,
    bool enable_switch_jump_table, TickCounter* tick_counter,
    JSHeapBroker* broker, size_t* max_unoptimized_frame_height,
    size_t* max_pushed_argument_count, bool collect_all_source_positons,
    base::Flags<CpuFeature> cpu_features,
    bool enable_instruction_scheduling = FLAG_turbo_instruction_scheduling,
    bool enable_roots_relative_addressing = false, bool trace_turbo = false);

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_SELECT_INSTRUCTIONS_H_
