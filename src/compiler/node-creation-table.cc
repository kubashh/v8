// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/node-creation-table.h"
#include "src/compiler/graph.h"
#include "src/compiler/node-aux-data.h"

namespace v8 {
namespace internal {
namespace compiler {

void NodeCreation::PrintJson(std::ostream& out) const {
  out << "{ "
      << "\"node\" : " << created_from() << ", \"reducer\" : \""
      << reducer_name() << "\""
      << "}";
}

class NodeCreationTable::Decorator final : public GraphDecorator {
 public:
  explicit Decorator(NodeCreationTable* node_creations)
      : node_creations_(node_creations) {}

  void Decorate(Node* node) final {
    node_creations_->SetNodeCreation(node, node_creations_->current_node_);
  }

 private:
  NodeCreationTable* node_creations_;
};

NodeCreationTable::NodeCreationTable(Graph* graph)
    : graph_(graph),
      decorator_(nullptr),
      current_node_(NodeCreation::Unknown()),
      table_(graph->zone()) {}

void NodeCreationTable::AddDecorator() {
  DCHECK_NULL(decorator_);
  decorator_ = new (graph_->zone()) Decorator(this);
  graph_->AddDecorator(decorator_);
}

void NodeCreationTable::RemoveDecorator() {
  DCHECK_NOT_NULL(decorator_);
  graph_->RemoveDecorator(decorator_);
  decorator_ = nullptr;
}

NodeCreation NodeCreationTable::GetNodeCreation(Node* node) const {
  return table_.Get(node);
}

void NodeCreationTable::SetNodeCreation(Node* node,
                                        const NodeCreation& position) {
  table_.Set(node, position);
}

void NodeCreationTable::PrintJson(std::ostream& os) const {
  os << "{";
  bool needs_comma = false;
  for (auto i : table_) {
    NodeCreation pos = i.second;
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
