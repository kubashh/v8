// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/node-origin-table.h"
#include "src/compiler/graph.h"
#include "src/compiler/node-aux-data.h"

namespace v8 {
namespace internal {
namespace compiler {

void NodeOrigin::PrintJson(std::ostream& out) const {
  out << "{ "
      << "\"nodeId\" : " << created_from() << ", \"reducer\" : \""
      << reducer_name() << "\""
      << "}";
}

class NodeOriginTable::Decorator final : public GraphDecorator {
 public:
  explicit Decorator(NodeOriginTable* node_origins)
      : node_origins_(node_origins) {}

  void Decorate(Node* node) final {
    node_origins_->SetNodeOrigin(node, node_origins_->current_node_);
  }

 private:
  NodeOriginTable* node_origins_;
};

NodeOriginTable::NodeOriginTable(Graph* graph)
    : graph_(graph),
      decorator_(nullptr),
      current_node_(NodeOrigin::Unknown()),
      table_(graph->zone()) {}

void NodeOriginTable::AddDecorator() {
  DCHECK_NULL(decorator_);
  decorator_ = new (graph_->zone()) Decorator(this);
  graph_->AddDecorator(decorator_);
}

void NodeOriginTable::RemoveDecorator() {
  DCHECK_NOT_NULL(decorator_);
  graph_->RemoveDecorator(decorator_);
  decorator_ = nullptr;
}

NodeOrigin NodeOriginTable::GetNodeOrigin(Node* node) const {
  return table_.Get(node);
}

void NodeOriginTable::SetNodeOrigin(Node* node, const NodeOrigin& position) {
  table_.Set(node, position);
}

void NodeOriginTable::PrintJson(std::ostream& os) const {
  os << "{";
  bool needs_comma = false;
  for (auto i : table_) {
    NodeOrigin pos = i.second;
    if (pos.IsKnown()) {
      if (needs_comma) {
        os << ",";
      }
      os << "\"" << i.first << "\""
         << ": ";
      pos.PrintJson(os);
      needs_comma = true;
    }
  }
  os << "}";
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
