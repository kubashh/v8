// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_STRING_BUILDER_OPTIMIZER_H_
#define V8_COMPILER_STRING_BUILDER_OPTIMIZER_H_

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "src/base/macros.h"
#include "src/base/optional.h"
#include "src/compiler/graph-assembler.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/node.h"
#include "src/compiler/schedule.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class Zone;

namespace compiler {

class JSGraphAssembler;
class NodeOriginTable;
class Schedule;
class SourcePositionTable;
class JSHeapBroker;

class OneOrTwoByteAnalysis final {
 public:
  OneOrTwoByteAnalysis(Graph* graph, Zone* zone, JSHeapBroker* broker)
      : states_(graph->NodeCount(), State::kUnknown, zone), broker_(broker) {}

  enum class State {
    kUnknown,
    kOneByte,  // Only 1-byte strings in the concatenation
    kTwoByte,  // At least one 2-byte string in the concatenation
    kCantKnow
  };

  static void PrintOneOrTwoByte(State state) {
    switch (state) {
      case State::kUnknown:
        PrintF("kUnknown\n");
        break;
      case State::kOneByte:
        PrintF("kOneByte\n");
        break;
      case State::kTwoByte:
        PrintF("kTwoByte\n");
        break;
      case State::kCantKnow:
        PrintF("kCantKnow\n");
        break;
      default:
        PrintF("HERE?!?! val = %d\n", static_cast<int>(state));
    }
  }

  State OneOrTwoByte(Node* node);
  base::Optional<double> TryGetRange(Node* node);

  static State MergeStates(State a, State b);

  JSHeapBroker* broker() { return broker_; }

 private:
  ZoneVector<State> states_;
  JSHeapBroker* broker_;
};

class V8_EXPORT_PRIVATE StringBuilderOptimizer final {
 public:
  StringBuilderOptimizer(JSGraph* jsgraph, Schedule* schedule, Zone* temp_zone,
                         JSHeapBroker* broker);

  bool BlockShouldFinalizeConcats(BasicBlock* block);
  ZoneVector<Node*> GetConcatsToFinalize(BasicBlock* block);

  OneOrTwoByteAnalysis::State GetOneOrTwoByte(Node* node);

  bool IsConcatEnd(Node* node);
  bool IsOptimizableConcatInput(Node* node);
  bool CanOptimizeConcat(Node* node);
  int GetConcatGroup(Node* node);
  bool IsFirstConcatInGroup(Node* node);

  void Run();

  JSGraph* jsgraph() const { return jsgraph_; }
  Graph* graph() const { return jsgraph_->graph(); }
  Schedule* schedule() const { return schedule_; }
  Zone* temp_zone() const { return temp_zone_; }
  JSHeapBroker* broker() const { return broker_; }

 private:
  enum class State : uint8_t {
    kUnvisited = 0,
    kBeginConcat,
    kInConcat,
    kEndConcat,
    kEndConcatLoopPhi,
    kPendingPhi,
    kInvalid,  // Something that we can't optimize
    kNumberOfState
  };

  struct Status {
    int id;
    State state;
  };

  Status GetStatus(Node* node) const {
    if (node->id() > status_.size()) {
      return Status{-1, State::kInvalid};
    } else {
      return status_[node->id()];
    }
  }
  void SetStatus(Node* node, State state, int id = -1) {
    // PrintF("SetStatus: Status{%d, %s}\n", id, StateToStr(state));
    DCHECK_NE(state, State::kUnvisited);
    DCHECK_IMPLIES(id != -1, state != State::kInvalid);
    if (node->id() >= status_.size()) {
      // We should really not allocate too many new nodes: the only new nodes we
      // allocate are constant inputs of nodes in concatenations that have
      // multiple uses. Thus, we don't grow {status_} exponentially, but rather
      // just linearly to save up some memory. "100" should be plenty for most
      // cases, while being small enough to not really cost too much memory in
      // cases where only 1 or 2 would have been enough.
      constexpr int growth_size = 100;
      status_.resize(node->id() + growth_size, Status{-1, State::kUnvisited});
    }
    status_[node->id()] = Status{id, state};
  }
  void UpdateStatus(Node* node, State state, int id = -1) {
    if (state == State::kInvalid) {
      status_[node->id()] = Status{-1, state};
    } else {
      if (id == -1) id = GetStatus(node).id;
      status_[node->id()] = Status{id, state};
    }
  }

  struct Concat {
    int id;
    Node* start;
    Node* max_len;
    Node* len;
    Node* backing_store;
  };

  const char* StateToStr(StringBuilderOptimizer::State state) {
    switch (state) {
      case State::kUnvisited:
        return "kUnvisited";
      case State::kBeginConcat:
        return "kBeginConcat";
      case State::kEndConcat:
        return "kEndConcat";
      case State::kEndConcatLoopPhi:
        return "kEndConcatLoopPhi";
      case State::kPendingPhi:
        return "kPendingPhi";
      case State::kInConcat:
        return "kInConcat";
      case State::kInvalid:
        return "kInvalid";
      case State::kNumberOfState:
        return "kNumberOfState";
    }
  }

  bool IsLoopPhi(Node* node) const {
    return node->opcode() == IrOpcode::kPhi &&
           schedule()->block(node)->IsLoopHeader();
  }
  bool LoopContains(Node* loop_phi, Node* node) {
    DCHECK(IsLoopPhi(loop_phi));
    return schedule()->block(loop_phi)->LoopContains(schedule()->block(node));
  }

  void SetConcatHasLoopPhi(unsigned int concat_id) {
    DCHECK_NE(concat_id, -1);
    if (concat_has_loop_phi_.size() <= concat_id) {
      concat_has_loop_phi_.resize(concat_id * 2 + 2, false);
    }
    DCHECK_LT(concat_id, concat_has_loop_phi_.size());
    concat_has_loop_phi_[concat_id] = true;
  }
  bool GetConcatHasLoopPhi(unsigned int concat_id) {
    return concat_id >= concat_has_loop_phi_.size()
               ? false
               : concat_has_loop_phi_[concat_id];
  }

  void ReplaceConcatInputIfNeeded(Node* node, int input_idx);

  void FinishConcatenations();
  bool CheckNodeUses(Node* node, Node* concat_child, Status status);
  bool CheckPreviousNodeUses(Node* child, Status status,
                             int input_if_loop_phi = 0);
  int GetPhiPredecessorsCommonId(Node* node);
  void VisitGraph();
  void VisitNode(Node* node, BasicBlock* block);

  static constexpr bool kAllowAnyStringOnTheRhs = false;

  JSGraph* jsgraph_;
  Schedule* schedule_;
  Zone* temp_zone_;
  JSHeapBroker* broker_;
  unsigned int concat_count_ = 0;
  NodeVector concat_starts_;
  ZoneVector<base::Optional<ZoneVector<Node*>>> trimmings_;
  ZoneVector<OneOrTwoByteAnalysis::State> concat_one_or_two_bytes_;

  ZoneVector<Status> status_;
  ZoneVector<BasicBlock*> current_predecessors_;
  ZoneVector<BasicBlock*> other_predecessors_;
  ZoneVector<bool> concat_has_loop_phi_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_STRING_BUILDER_OPTIMIZER_H_
