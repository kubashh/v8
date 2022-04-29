// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/simple-scheduler.h"

#include "src/compiler/graph.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

SimpleScheduler::SimpleScheduler(Zone* zone, Graph* graph)
    : graph_(graph), dominator_level_(zone), immediate_dominator_(zone) {
  ComputeDominatorsLevel();
}

void SimpleScheduler::ComputeDominatorsLevel() {
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

Node* SimpleScheduler::GetImmediateDominator(Node* node) {
  DCHECK(!NodeProperties::IsControl(node));

  auto it = immediate_dominator_.find(node);
  if (it != immediate_dominator_.end()) return it->second;

  Node* idom = nullptr;
  // For phi node, the immediate dominator is its control node.
  if (NodeProperties::IsPhi(node)) {
    idom = NodeProperties::GetControlInput(node);
  } else {
    // For non-phi node, the immediate dominator is the immediate dominator
    // of its input with maximal level.
    int maximal_level = -1;
    if (node->InputCount() == 0) {
      idom = graph_->start();
    }
    for (int i = 0; i < node->InputCount(); ++i) {
      Node* input = node->InputAt(i);
      Node* dominator = nullptr;
      if (NodeProperties::IsControl(input)) {
        dominator = input;
      } else {
        dominator = GetImmediateDominator(input);
      }
      int level = GetDominatorLevel(dominator);
      if (level > maximal_level) {
        idom = dominator;
      }
    }
  }

  DCHECK(idom != nullptr);
  SetImmediateDominator(node, idom);
  return idom;
}

bool SimpleScheduler::SameBasicBlock(Node* node0, Node* node1) {
  Node* idom0 =
      NodeProperties::IsControl(node0) ? node0 : GetImmediateDominator(node0);
  Node* idom1 =
      NodeProperties::IsControl(node1) ? node1 : GetImmediateDominator(node1);
  return idom0 == idom1;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
