// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_DECOMPRESSION_ELIMINATION_H_
#define V8_COMPILER_DECOMPRESSION_ELIMINATION_H_

#include "src/compiler/graph-reducer.h"

namespace v8 {
namespace internal {
namespace compiler {

// Performs elimination of redundant decompressions within the graph.
class V8_EXPORT_PRIVATE DecompressionElimination final
    : public NON_EXPORTED_BASE(AdvancedReducer) {
 public:
  explicit DecompressionElimination(Editor* editor);
  ~DecompressionElimination() final = default;

  const char* reducer_name() const override {
    return "DecompressionElimination";
  }

  Reduction Reduce(Node* node) final;

 private:
  // Removes direct Decompressions & Compressions, going from
  //     Parent <- Decompression <- Compression <- Child
  // to
  //     Parent <- Child
  // Replaces node with its grandparent if its input has inputOpcode as its
  // opcode.
  Reduction ReduceDecompression(Node* node, IrOpcode::Value inputOpcode);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_DECOMPRESSION_ELIMINATION_H_
