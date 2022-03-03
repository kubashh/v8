// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BRANCH_CONDITION_DUPLICATOR_H_
#define V8_COMPILER_BRANCH_CONDITION_DUPLICATOR_H_

#include "src/base/macros.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/node.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declare.
class Graph;

// BranchConditionDuplicator makes sure that the condition nodes of branches are
// used only once. When it finds a branch node whose condition has multiples
// uses, this condition is duplicated.
//
// Doing this enables the InstructionSelector to generate more efficient code
// for branches. For instance, consider this code:
//
//     if (a + b) { /* some code */ }
//     if (a + b) { /* more code */ }
//
// If the same node is used for both "a+b", then the generated code will be
// something like:
//
//     x = add a, b
//     cmp x, 0
//     jz
//     ...
//     cmp x, 0
//     jz
//
// Instead, if a different node is used for both "a+b", we can avoid storing the
// result of "a+b" in a dedicated register, and can also avoid those "cmp"
// instructions. For instance, we could instead generate:
//
//     add a, b
//     jnz
//     ...
//     add a, b
//     jnz
class V8_EXPORT_PRIVATE BranchConditionDuplicator final {
 public:
  BranchConditionDuplicator(Zone* zone, Graph* graph);
  ~BranchConditionDuplicator() = default;

  void Reduce();

  Node* DuplicateNode(Node* node);
  void DuplicateConditionIfNeeded(Node* node);
  void Enqueue(Node* node);
  void VisitNode(Node* node);
  void WalkControlDepsAndDupNodesIfNeeded();

 private:
  Graph* const graph_;
  ZoneQueue<Node*> to_visit_;
  NodeMarker<bool> seen_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BRANCH_CONDITION_DUPLICATOR_H_
