// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/node-observer.h"

#include "src/compiler/node-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

ObservableNodeState::ObservableNodeState(const Node* node, Zone* zone)
    : id_(node->id()),
      op_(node->op()),
      type_(NodeProperties::GetTypeOrAny(node)),
      inputs_(zone),
      uses_(zone) {
  for (int i = 0; i < node->InputCount(); i++) {
    inputs_.insert(node->InputAt(i));
  }

  Node::Uses uses(const_cast<Node*>(node));
  for (const Node* use : uses) {
    uses_.insert(use);
  }
}

void ObserveNodeManager::StartObserving(Node* node, NodeObserver* observer) {
  DCHECK_NOT_NULL(node);
  DCHECK_NOT_NULL(observer);
  DCHECK(observations_.find(node->id()) == observations_.end());

  NodeObserver::Observation observation = observer->OnNodeCreated(node);
  if (observation == NodeObserver::Observation::kContinue) {
    observations_[node->id()] =
        zone_->New<NodeObservation>(observer, node, zone_);
  } else {
    DCHECK_EQ(observation, NodeObserver::Observation::kStop);
  }
}

void ObserveNodeManager::OnNodeChanged(const char* reducer_name,
                                       const Node* old_node,
                                       const Node* new_node) {
  const auto it = observations_.find(old_node->id());
  if (it == observations_.end()) return;

  ObservableNodeState new_state{new_node, zone_};
  NodeObservation* observation = it->second;
  if (observation->state == new_state) return;

  ObservableNodeState old_state = observation->state;
  NodeObserver::Observation result =
      observation->observer->OnNodeChanged(reducer_name, new_node, old_state);

  // Update the state before notifying changes to inputs and uses, to avoid
  // loops.
  observation->state = new_state;

  NotifyChangedNodes(reducer_name, old_state.Inputs(), new_state.Inputs());
  NotifyChangedNodes(reducer_name, old_state.Uses(), new_state.Uses());

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

void ObserveNodeManager::NotifyChangedNodes(
    const char* reducer_name, const ZoneSet<const Node*>& prev_nodes,
    const ZoneSet<const Node*>& current_nodes) {
  std::set<const Node*> changed;
  std::set_symmetric_difference(prev_nodes.begin(), prev_nodes.end(),
                                current_nodes.begin(), current_nodes.end(),
                                std::inserter(changed, changed.end()));
  for (const Node* node : changed) {
    OnNodeChanged(reducer_name, node, node);
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
