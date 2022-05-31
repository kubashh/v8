// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/linear-scheduler.h"

#include "src/compiler/graph.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

LinearScheduler::LinearScheduler(Zone* zone, Graph* graph)
    : graph_(graph), dominator_level_(zone), immediate_dominator_(zone) {
  ComputeDominatorsLevel();
}

void LinearScheduler::ComputeDominatorsLevel() {
  Node* start = graph_->start();
  SetDominatorLevel(start, 0);

  // Do BFS from the start node and compute the level of
  // each dominator.
  std::queue<Node*> queue({start});
  while (!queue.empty()) {
    Node* node = queue.front();
    int level = GetDominatorLevel(node);
    queue.pop();
    for (Edge const edge : node->use_edges()) {
      if (!NodeProperties::IsControlEdge(edge)) continue;
      Node* use = edge.from();
      if (dominator_level_.find(use) == dominator_level_.end() &&
          use->opcode() != IrOpcode::kEnd) {
        SetDominatorLevel(use, level + 1);
        queue.push(use);
      }
    }
  }
}

Node* LinearScheduler::GetImmediateDominator(Node* node) {
  DCHECK(!NodeProperties::IsControl(node));

  auto it = immediate_dominator_.find(node);
  if (it != immediate_dominator_.end()) return it->second;

  std::stack<NodeState> stack;
  stack.push({node, nullptr, 0});
  Node* idom = nullptr;
  while (!stack.empty()) {
    NodeState& top = stack.top();
    if (NodeProperties::IsPhi(top.node)) {
      // For phi node, the immediate dominator is its control node.
      idom = NodeProperties::GetControlInput(top.node);
    } else if (top.node->InputCount() == 0) {
      // For node without inputs, the immediate dominator is start node.
      idom = graph_->start();
    } else {
      // For others, the immediate dominator is one of its inputs' immediate
      // dominator with maximal level.
      if (top.input_index == top.node->InputCount()) {
        // All inputs are visited, set immediate dominator.
        idom = top.dominator;
      } else {
        // Visit current input and find its immediate dominator.
        Node* input = top.node->InputAt(top.input_index);
        Node* input_idom = nullptr;
        if (NodeProperties::IsControl(input)) {
          input_idom = input;
        } else {
          auto it = immediate_dominator_.find(input);
          if (it != immediate_dominator_.end()) input_idom = it->second;
        }
        if (input_idom != nullptr) {
          if (top.dominator == nullptr || GetDominatorLevel(top.dominator) <
                                              GetDominatorLevel(input_idom)) {
            top.dominator = input_idom;
          }
          top.input_index += 1;
        } else {
          top.input_index += 1;
          stack.push({input, nullptr, 0});
        }
        continue;
      }
    }

    // Found top's immediate dominator, set it to the cache and pop it out of
    // the stack.
    SetImmediateDominator(top.node, idom);
    stack.pop();
    // Update immediate dominator of top's use.
    if (!stack.empty()) {
      NodeState& use = stack.top();
      if (use.dominator == nullptr ||
          GetDominatorLevel(use.dominator) < GetDominatorLevel(top.dominator)) {
        use.dominator = top.dominator;
      }
    }
  }

  DCHECK(idom != nullptr);
  return idom;
}

bool LinearScheduler::SameBasicBlock(Node* node0, Node* node1) {
  Node* idom0 =
      NodeProperties::IsControl(node0) ? node0 : GetImmediateDominator(node0);
  Node* idom1 =
      NodeProperties::IsControl(node1) ? node1 : GetImmediateDominator(node1);
  return idom0 == idom1;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
