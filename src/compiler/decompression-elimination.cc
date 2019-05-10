// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/decompression-elimination.h"

namespace v8 {
namespace internal {
namespace compiler {

DecompressionElimination::DecompressionElimination(Editor* editor)
    : AdvancedReducer(editor) {}

Reduction DecompressionElimination::ReduceCompress(
    Node* node, IrOpcode::Value inputOpcode) {
  DCHECK(node->opcode() == IrOpcode::kChangeTaggedToCompressed ||
         node->opcode() == IrOpcode::kChangeTaggedSignedToCompressedSigned ||
         node->opcode() == IrOpcode::kChangeTaggedPointerToCompressedPointer);

  DCHECK_EQ(node->InputCount(), 1);
  Node* input_node = node->InputAt(0);
  if (input_node->opcode() == inputOpcode) {
    DCHECK_EQ(input_node->InputCount(), 1);
    return Replace(input_node->InputAt(0));
  } else {
    return NoChange();
  }
}

Reduction DecompressionElimination::Reduce(Node* node) {
  DisallowHeapAccess no_heap_access;
  switch (node->opcode()) {
    case IrOpcode::kChangeTaggedToCompressed:
      return ReduceCompress(node, IrOpcode::kChangeCompressedToTagged);
    case IrOpcode::kChangeTaggedSignedToCompressedSigned:
      return ReduceCompress(node,
                            IrOpcode::kChangeCompressedSignedToTaggedSigned);
    case IrOpcode::kChangeTaggedPointerToCompressedPointer:
      return ReduceCompress(node,
                            IrOpcode::kChangeCompressedPointerToTaggedPointer);
    default:
      break;
  }
  return NoChange();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
