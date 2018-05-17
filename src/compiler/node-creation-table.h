// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NODE_CREATION_TABLE_H_
#define V8_COMPILER_NODE_CREATION_TABLE_H_

#include <limits>

#include "src/base/compiler-specific.h"
#include "src/compiler/node-aux-data.h"
#include "src/globals.h"
#include "src/source-position.h"

namespace v8 {
namespace internal {
namespace compiler {

class NodeCreation {
 public:
  NodeCreation(const char* reducer_name, NodeId created_from)
      : reducer_name_(reducer_name), created_from_(created_from) {}
  NodeCreation(const NodeCreation& other) = default;
  static NodeCreation Unknown() { return NodeCreation(); }

  bool IsKnown() { return created_from_ >= 0; }
  int64_t created_from() const { return created_from_; }
  const char* reducer_name() const { return reducer_name_; }

  void PrintJson(std::ostream& out) const;

 private:
  NodeCreation()
      : reducer_name_(""), created_from_(std::numeric_limits<int64_t>::min()) {}
  const char* reducer_name_;
  int64_t created_from_;
};

class V8_EXPORT_PRIVATE NodeCreationTable final
    : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  class Scope final {
   public:
    Scope(NodeCreationTable* node_creations, const char* reducer_name,
          Node* node)
        : node_creations_(node_creations),
          prev_node_(node_creations->current_node_) {
      node_creations->current_node_ = NodeCreation(reducer_name, node->id());
    }

    ~Scope() { node_creations_->current_node_ = prev_node_; }

   private:
    NodeCreationTable* const node_creations_;
    NodeCreation const prev_node_;
    DISALLOW_COPY_AND_ASSIGN(Scope);
  };

  explicit NodeCreationTable(Graph* graph);

  void AddDecorator();
  void RemoveDecorator();

  NodeCreation GetNodeCreation(Node* node) const;
  void SetNodeCreation(Node* node, const NodeCreation& position);

  void SetCurrentPosition(const NodeCreation& pos) { current_node_ = pos; }

  void PrintJson(std::ostream& os) const;

 private:
  class Decorator;

  Graph* const graph_;
  Decorator* decorator_;
  NodeCreation current_node_;
  NodeAuxData<NodeCreation, NodeCreation::Unknown> table_;

  DISALLOW_COPY_AND_ASSIGN(NodeCreationTable);
};

inline bool operator==(const NodeCreation& lhs, const NodeCreation& rhs) {
  return lhs.reducer_name() == rhs.reducer_name() &&
         lhs.created_from() == rhs.created_from();
}

inline bool operator!=(const NodeCreation& lhs, const NodeCreation& rhs) {
  return !(lhs == rhs);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NODE_CREATION_TABLE_H_
