// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/node-observer.h"

#include "src/compiler/node-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

ObservableNodeState::ObservableNodeState(Node* node, Zone* zone)
    : id_(0), op_(nullptr), type_(), inputs_(zone), uses_(zone) {
  if (node) {
    id_ = node->id();
    op_ = node->op();
    type_ = NodeProperties::GetTypeOrAny(node);

    for (int i = 0; i < node->InputCount(); i++) {
      inputs_.push_back(node->InputAt(i)->id());
    }

    Node::Uses uses(node);
    for (const Node* use : uses) {
      uses_.push_back(use->id());
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
