// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_LINEAR_SCHEDULER_H_
#define V8_COMPILER_LINEAR_SCHEDULER_H_

#include "src/base/flags.h"
#include "src/common/globals.h"
#include "src/compiler/node.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/zone-stats.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

// A simple, linear-time scheduler to check whether two nodes are in a same
// basic block without actually building basic block.
class V8_EXPORT_PRIVATE LinearScheduler {
 public:
  explicit LinearScheduler(Zone* zone, Graph* graph);
  bool SameBasicBlock(Node* node0, Node* node1);

 private:
  // Compute the level of each dominators. The level is defined
  // by the shortest distance from the start node.
  void ComputeDominatorsLevel();

  int GetDominatorLevel(Node* dominator) const {
    auto it = dominator_level_.find(dominator);
    DCHECK(it != dominator_level_.end());
    return it->second;
  }

  void SetDominatorLevel(Node* dominator, int level) {
    DCHECK(dominator_level_.find(dominator) == dominator_level_.end());
    dominator_level_[dominator] = level;
  }

  Node* GetImmediateDominator(Node* node);

  void SetImmediateDominator(Node* node, Node* idom) {
    immediate_dominator_[node] = idom;
  }

  Graph* graph_;
  // A map from a control node to the dominator level of the corresponding basic
  // block.
  ZoneMap<Node*, int> dominator_level_;
  // A map from a non-contol node to its immediate dominator.
  ZoneMap<Node*, Node*> immediate_dominator_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_LINEAR_SCHEDULER_H_
