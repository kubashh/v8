// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/loop-unrolling.h"

#include "src/codegen/tick-counter.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/loop-analysis.h"

namespace v8 {
namespace internal {
namespace compiler {

void LoopUnroller::Unroll() {
  for (const LoopTree::Loop* loop : loop_tree_->inner_loops()) UnrollLoop(loop);
}

void LoopUnroller::UnrollLoop(const LoopTree::Loop* loop) {
  Node* loop_node = loop_tree_->GetLoopControl(loop);

  // No back-jump to the loop header means this is not really a loop.
  if (loop_node->InputCount() < 2) return;

  size_t copied_size = loop->TotalSize() * 2;
  NodeVector pairs(tmp_zone_);
  NodeCopier copier(graph_, copied_size, &pairs);

  // Copy all the nodes of loop. The copies will become the second loop
  // iteration.
  copier.CopyNodes(graph_, tmp_zone_, graph_->NewNode(common_->Dead()),
                   loop_tree_->LoopNodes(loop), source_positions_,
                   node_origins_);

  Node* loop_node_copy = copier.map(loop_node);

  for (Node* node : loop_tree_->LoopNodes(loop)) {
    switch (node->opcode()) {
      case IrOpcode::kBranch:
        if (node->InputAt(0)->opcode() == IrOpcode::kStackPointerGreaterThan) {
          // Remove stack check for the second iteration by replacing its
          // condition with {true}. Dead code elimination will clean up the
          // unreachable nodes later.
          copier.map(node)->ReplaceInput(
              0, graph_->NewNode(common_->Int32Constant(1)));
        }
        break;

      case IrOpcode::kLoopExit:
        if (node->InputAt(1) == loop_node) {
          // Create a merge from both loop iterations.
          Node* merge_node =
              graph_->NewNode(common_->Merge(2), node, copier.map(node));
          for (Edge use_edge : node->use_edges()) {
            Node* use = use_edge.from();
            if (loop_tree_->Contains(loop, use)) {
              const Operator* phi_operator;
              if (use->opcode() == IrOpcode::kLoopExitEffect) {
                phi_operator = common_->EffectPhi(2);
              } else {
                DCHECK(use->opcode() == IrOpcode::kLoopExitValue);
                phi_operator =
                    common_->Phi(LoopExitValueRepresentationOf(use->op()), 2);
              }
              Node* phi = graph_->NewNode(phi_operator, use, copier.map(use),
                                          merge_node);
              use->ReplaceUses(phi);
              // Repair phi which we just broke.
              phi->ReplaceInput(0, use);
            } else if (use != merge_node) {
              use->ReplaceInput(use_edge.index(), merge_node);
            }
          }
        }
        break;

      default:
        break;
    }
  }

  // Remove the copy loop node. All control uses of iteration 2 should now point
  // to the control dependency of the original loop header, except phi nodes,
  // which will be removed anyway.
  for (Edge edge : loop_node_copy->use_edges()) {
    if (!NodeProperties::IsPhi(edge.from())) {
      edge.from()->ReplaceInput(edge.index(), loop_node->InputAt(1));
    }
  }
  // Change the control dependency of the original loop to point to the control
  // dependency of the second iteration.
  loop_node->ReplaceInput(1, loop_node_copy->InputAt(1));

  for (Node* loop_use : loop_node->uses()) {
    if (loop_tree_->Contains(loop, loop_use) &&
        NodeProperties::IsPhi(loop_use)) {
      Node* phi_copy = copier.map(loop_use);
      int control_index = NodeProperties::FirstControlIndex(loop_use);
      // Phis depending on the loop header in the second iteration (i.e.,
      // picking between a value from within the loop and before the loop)
      // should be replaced with the corresponding value in the first iteration.
      phi_copy->ReplaceUses(loop_use->InputAt(control_index - 1));
      // Phis in the first iteration should point to the second iteration
      // instead of the first.
      for (int i = 0; i < control_index; i++) {
        loop_use->ReplaceInput(i, phi_copy->InputAt(i));
      }
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
