// Copyright 2020 the V8 project authors. All rights reserved.
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
  ObservableNodeState(Node* node, Zone* zone);

  operator bool() { return op_ != nullptr; }

  uint32_t id() const { return id_; }
  const Operator* op() const { return op_; }
  int16_t opcode() const { return op_->opcode(); }
  Type type() const { return type_; }

  size_t InputCount() const { return inputs_.size(); }
  const ZoneVector<NodeId>& Inputs() const { return inputs_; }

  size_t UseCount() const { return uses_.size(); }
  const ZoneVector<NodeId>& Uses() const { return uses_; }

 private:
  uint32_t id_;
  const Operator* op_;
  Type type_;
  ZoneVector<NodeId> inputs_;
  ZoneVector<NodeId> uses_;
};

inline bool operator==(const ObservableNodeState& lhs,
                       const ObservableNodeState& rhs) {
  return lhs.id() == rhs.id() && lhs.op() == rhs.op() &&
         lhs.type() == rhs.type() && lhs.Inputs() == rhs.Inputs() &&
         lhs.Uses() == rhs.Uses();
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
  virtual ~NodeObserver() = default;
  virtual Observation OnNodeCreated(const Node* node) {
    return Observation::kContinue;
  }
  virtual Observation OnNodeChanged(const char* reducer_name, const Node* node,
                                    const ObservableNodeState& old_state) {
    return Observation::kContinue;
  }

 private:
  NodeObserver(const NodeObserver&) = delete;
  NodeObserver& operator=(const NodeObserver&) = delete;
};

class NodeObservation : public ZoneObject {
 public:
  explicit NodeObservation(NodeObserver* observer, Zone* zone)
      : zone_(zone),
        node_(nullptr),
        state_(nullptr, zone),
        observer_(observer) {
    DCHECK_NOT_NULL(observer_);
  }

  NodeObserver::Observation Begin(Node* node) {
    DCHECK_NULL(node_);
    DCHECK(!state_);
    node_ = node;
    state_ = StateOfNode(node);

    return observer_->OnNodeCreated(node);
  }

  NodeObserver::Observation Update(const char* reducer_name,
                                   const ObservableNodeState& new_state) {
    DCHECK(state_);
    DCHECK_NOT_NULL(observer_);
    if (state_ != new_state) {
      auto obs = observer_->OnNodeChanged(reducer_name, node_, state_);
      state_ = new_state;
      return obs;
    }
    return NodeObserver::Observation::kContinue;
  }

  ObservableNodeState StateOfNode(Node* node) {
    return ObservableNodeState{node, zone_};
  }

 private:
  Zone* zone_;
  Node* node_;
  ObservableNodeState state_;
  NodeObserver* observer_;
};

class ObserveNodeManager : public ZoneObject {
 public:
  explicit ObserveNodeManager(Zone* zone) : observations_(zone) {}

  void StartObserving(const Node* node, NodeObservation* observation) {
    observations_[node->id()] = observation;
  }

  void OnNodeChanged(const char* reducer_name, const Node* old_node,
                     Node* new_node) {
    const auto it = observations_.find(old_node->id());
    if (it == observations_.end()) return;

    NodeObservation* observation = it->second;
    NodeObserver::Observation result =
        observation->Update(reducer_name, observation->StateOfNode(new_node));
    if (result == NodeObserver::Observation::kStop) {
      observations_.erase(old_node->id());
    } else {
      DCHECK_EQ(result, NodeObserver::Observation::kContinue);
      if (old_node != new_node) {
        observations_.erase(old_node->id());
        observations_[new_node->id()] = observation;
      }
    }
  }

 private:
  ZoneMap<NodeId, NodeObservation*> observations_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NODE_OBSERVER_H_
