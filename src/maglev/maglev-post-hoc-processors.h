// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_POST_HOC_PROCESSORS_H_
#define V8_MAGLEV_MAGLEV_POST_HOC_PROCESSORS_H_

#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-graph.h"

namespace v8::internal::maglev {

// The PostHocProcessors do some optimizations and prepare for register
// allocation and code generation.
//
// Optimizations:
//   - Finding and removing dead nodes
//   - Escape analysis
//   - Cleaning up Identity nodes
//
// Preparing for regalloc/codegen:
//   - Collect input/output location constraints
//   - Find the maximum number of stack arguments passed to calls
//   - Collect use information, for SSA liveness and next-use distance.
//   - Mark

void RunPostHocProcessors(LocalIsolate* local_isolate,
                          MaglevCompilationInfo* compilation_info,
                          Graph* graph);

}  // namespace v8::internal::maglev

#endif  // V8_MAGLEV_MAGLEV_POST_HOC_PROCESSORS_H_
