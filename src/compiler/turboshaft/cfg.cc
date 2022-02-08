// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/cfg.h"

#include <iomanip>

#include "src/compiler/turboshaft/reducer.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

std::ostream& operator<<(std::ostream& os, const Graph& graph) {
  for (const Block& block : graph.blocks()) {
    os << "--- BLOCK B" << ToUnderlyingType(block.index);
    if (!block.predecessors.empty()) {
      os << " <- ";
      bool first = true;
      for (const Block* pred : block.predecessors) {
        if (!first) os << ", ";
        os << "B" << ToUnderlyingType(pred->index);
        first = false;
      }
    }
    os << " ---\n";
    for (const Operation& op : graph.operations(block)) {
      os << std::setw(5) << graph.Index(op).id() << ": " << op << "\n";
    }
  }
  return os;
}

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8
