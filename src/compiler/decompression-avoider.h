// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_DECOMPRESSION_AVOIDER_H_
#define V8_COMPILER_DECOMPRESSION_AVOIDER_H_

#include "src/compiler/machine-operator.h"
#include "src/compiler/node-marker.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declare.
class Graph;

// DecompressionAvoider purpose is to avoid the full decompression on Loads
// whenever possible. Its scope is narrowed down to TaggedPointer and AnyTagged,
// since TaggedSigned avoids full decompression always.
class V8_EXPORT_PRIVATE DecompressionAvoider final {
 public:
  // When creating the DecompressionAvoider, we assign States to the nodes.
  DecompressionAvoider(Zone* zone, Graph* graph,
                       MachineOperatorBuilder* machine);
  ~DecompressionAvoider();

  // Go through the already marked nodes and changed the operation for the loads
  // that can avoid the full decompression.
  void ChangeLoads();

 private:
  // State refers to the node's state as follows:
  // * kUnvisited === This node has yet to be visited.
  // * kCanUse32bits === This node either has been visited, or is on to_visit_.
  // We coulnd't find a reason the output of this node needs to be in 64 bits.
  // * kNeeds64bits === This node either has been visited, or is on to_visit_.
  // The output of this node needs to be in 64 bits.
  enum class State : uint8_t { kUnvisited, kCanUse32bits, kNeeds64bits };

  // Goes through the nodes to mark them all as appropiate. It will visit each
  // node at most twice: only when the node was unvisited, then marked as
  // kCanUse32bits and visited, and finally marked as kNeeds64bits and visited.
  void MarkNodes();

  // Mark node's input as appropiate, according to node's opcode. Some input
  // State may be updated, and therefore has to be revisited.
  void MarkNodeInputs(Node* node);

  // Mark node's State to be state. We only do this if we have new information,
  // i.e either if:
  // * We are marking an unvisited node, or
  // * We are marking a node as needing 64 bits when we previously had the
  // information that it could output 32 bits. Also, we store the TaggedPointer
  // and AnyTagged loads that have their state set as kCanUse32bits.
  void MarkAs(Node* const node, State state);

  V8_INLINE bool Needs64bits(Node* const node) {
    return states_.Get(node) == State::kNeeds64bits;
  }

  V8_INLINE bool IsMachineLoad(Node* const node) {
    const IrOpcode::Value opcode = node->opcode();
    return opcode == IrOpcode::kLoad || opcode == IrOpcode::kPoisonedLoad ||
           opcode == IrOpcode::kProtectedLoad ||
           opcode == IrOpcode::kUnalignedLoad;
  }

  Graph* graph() const { return graph_; }
  MachineOperatorBuilder* machine() const { return machine_; }

  Graph* const graph_;
  MachineOperatorBuilder* const machine_;
  NodeMarker<State> states_;
  // Queue of nodes to be visited.
  NodeQueue to_visit_;
  // Contains the AnyTagged and TaggedPointer loads that can avoid the full
  // decompression. In a way, it functions as a NodeSet since each node will be
  // at most once. It's a Vector since we care about insertion speed.
  NodeVector compressed_loads_;

  DISALLOW_COPY_AND_ASSIGN(DecompressionAvoider);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_DECOMPRESSION_AVOIDER_H_
