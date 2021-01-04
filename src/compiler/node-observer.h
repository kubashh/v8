// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NODE_OBSERVER_H_
#define V8_COMPILER_NODE_OBSERVER_H_

#include "src/compiler/node.h"
#include "src/compiler/operator.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

class Node;
class Operator;

class ObservableNodeState {
 public:
  ObservableNodeState(const Node* node, Zone* zone);

  uint32_t id() const { return id_; }
  const Operator* op() const { return op_; }
  int16_t opcode() const { return op_->opcode(); }
  Type type() const { return type_; }

 private:
  uint32_t id_;
  const Operator* op_;
  Type type_;
};

inline bool operator==(const ObservableNodeState& lhs,
                       const ObservableNodeState& rhs) {
  return lhs.id() == rhs.id() && lhs.op() == rhs.op() &&
         lhs.type() == rhs.type();
}

inline bool operator!=(const ObservableNodeState& lhs,
                       const ObservableNodeState& rhs) {
  return !operator==(lhs, rhs);
}

class NodeObserver : public ZoneObject {
 public:
  enum class Observation {
    kContinue,
    kStop,
  };

  NodeObserver() = default;
  virtual ~NodeObserver() = 0;

  NodeObserver(const NodeObserver&) = delete;
  NodeObserver& operator=(const NodeObserver&) = delete;

  virtual Observation OnNodeCreated(const Node* node) {
    return Observation::kContinue;
  }

  virtual Observation OnNodeChanged(const char* reducer_name, const Node* node,
                                    const ObservableNodeState& old_state) {
    return Observation::kContinue;
  }
};
inline NodeObserver::~NodeObserver() {}

struct NodeObservation : public ZoneObject {
  NodeObservation(NodeObserver* node_observer, const Node* node, Zone* zone)
      : observer(node_observer), state(node, zone) {
    DCHECK_NOT_NULL(node_observer);
  }

  NodeObserver* observer;
  ObservableNodeState state;
};

class ObserveNodeManager : public ZoneObject {
 public:
  explicit ObserveNodeManager(Zone* zone) : zone_(zone), observations_(zone) {}

  void StartObserving(Node* node, NodeObserver* observer);
  void OnNodeChanged(const char* reducer_name, const Node* old_node,
                     const Node* new_node);

 private:
  Zone* zone_;
  ZoneMap<NodeId, NodeObservation*> observations_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NODE_OBSERVER_H_
