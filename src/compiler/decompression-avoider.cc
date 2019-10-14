// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/decompression-avoider.h"

#include "src/compiler/graph.h"
#include "src/compiler/node-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

DecompressionAvoider::DecompressionAvoider(Zone* zone, Graph* graph,
                                           MachineOperatorBuilder* machine)
    : graph_(graph),
      machine_(machine),
      states_(graph, 3),
      to_visit_(zone),
      compressed_loads_(zone) {
  compressed_loads_.reserve(graph->NodeCount());
  MarkNodes();
}

DecompressionAvoider::~DecompressionAvoider() = default;

void DecompressionAvoider::MarkNodes() {
  MarkAs(graph()->end(), State::kCanUse32bits);
  while (!to_visit_.empty()) {
    Node* const node = to_visit_.front();
    to_visit_.pop();
    MarkNodeInputs(node);
  }
}

void DecompressionAvoider::MarkNodeInputs(Node* node) {
  // Mark the value inputs.
  switch (node->opcode()) {
    case IrOpcode::kLoad:           // Fall through.
    case IrOpcode::kPoisonedLoad:   // Fall through.
    case IrOpcode::kProtectedLoad:  // Fall through.
    case IrOpcode::kUnalignedLoad:
      DCHECK_EQ(node->op()->ValueInputCount(), 2);
      MarkAs(node->InputAt(0), State::kNeeds64bits);
      MarkAs(node->InputAt(1), State::kCanUse32bits);
      break;
    case IrOpcode::kStore:           // Fall through.
    case IrOpcode::kProtectedStore:  // Fall through.
    case IrOpcode::kUnalignedStore:
      DCHECK_EQ(node->op()->ValueInputCount(), 3);
      MarkAs(node->InputAt(0), State::kNeeds64bits);
      MarkAs(node->InputAt(1), State::kCanUse32bits);
      MarkAs(node->InputAt(2), State::kCanUse32bits);
      break;
    default:
      // To be conservative, we assume that all value inputs need to be 64 bits
      // unless noted otherwise.
      for (int i = 0; i < node->op()->ValueInputCount(); ++i) {
        MarkAs(node->InputAt(i), State::kNeeds64bits);
      }
      break;
  }

  // We always mark the non-value input nodes as kCanUse32bits so that they will
  // be visited. If they need to be kNeeds64bits, they will be marked as such in
  // a future pass.
  for (int i = node->op()->ValueInputCount(); i < node->InputCount(); ++i) {
    MarkAs(node->InputAt(i), State::kCanUse32bits);
  }
}

void DecompressionAvoider::MarkAs(Node* const node, State state) {
  DCHECK_NE(state, State::kUnvisited);
  State previous_state = states_.Get(node);
  // Only update the state if we have relevant new information.
  if (previous_state == State::kUnvisited ||
      (previous_state == State::kCanUse32bits &&
       state == State::kNeeds64bits)) {
    states_.Set(node, state);
    to_visit_.push(node);

    // In the case of a TaggedPointer or TaggedAny Load that can be done in 32
    // bits, we save it in compressed_loads_ to be changed later if necessary.
    if (state == State::kCanUse32bits && IsMachineLoad(node) &&
        CanBeTaggedPointer(LoadRepresentationOf(node->op()).representation())) {
      compressed_loads_.push_back(node);
    }
  }
}

void DecompressionAvoider::ChangeLoads() {
  for (Node* const node : compressed_loads_) {
    // compressed_loads_ contains all the nodes that once had the
    // State::kCanUse32bits. If we later updated the state to be 64 bits, then
    // we have to ignore them. This is less costly than removing them from the
    // compressed_loads_ NodeVector when we update them to State::Needs64bits.
    if (Needs64bits(node)) continue;

    // Change to a Compressed MachRep to avoid the full decompression.
    LoadRepresentation load_rep = LoadRepresentationOf(node->op());
    LoadRepresentation compressed_load_rep;
    if (load_rep == MachineType::AnyTagged()) {
      compressed_load_rep = MachineType::AnyCompressed();
    } else {
      DCHECK_EQ(load_rep, MachineType::TaggedPointer());
      compressed_load_rep = MachineType::CompressedPointer();
    }

    // Change to the Operator with the Compressed MachineRepresentation.
    switch (node->opcode()) {
      case IrOpcode::kLoad:
        NodeProperties::ChangeOp(node, machine()->Load(compressed_load_rep));
        break;
      case IrOpcode::kPoisonedLoad:
        NodeProperties::ChangeOp(node,
                                 machine()->PoisonedLoad(compressed_load_rep));
        break;
      case IrOpcode::kProtectedLoad:
        NodeProperties::ChangeOp(node,
                                 machine()->ProtectedLoad(compressed_load_rep));
        break;
      case IrOpcode::kUnalignedLoad:
        NodeProperties::ChangeOp(node,
                                 machine()->UnalignedLoad(compressed_load_rep));
        break;
      default:
        UNREACHABLE();
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
