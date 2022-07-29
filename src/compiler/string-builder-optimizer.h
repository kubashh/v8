// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_STRING_BUILDER_OPTIMIZER_H_
#define V8_COMPILER_STRING_BUILDER_OPTIMIZER_H_

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "src/base/macros.h"
#include "src/compiler/graph-assembler.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/node.h"
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

class V8_EXPORT_PRIVATE StringBuilderOptimizer final {
  // TODO(dm): handle 2-byte strings.
 public:
  StringBuilderOptimizer(JSGraph* jsgraph, Schedule* schedule, Zone* temp_zone,
                         JSHeapBroker* broker);

  bool IsConcatEnd(Node* node);
  bool IsOptimizableConcatInput(Node* node);
  bool CanOptimizeConcat(Node* node);
  int GetConcatGroup(Node* node);
  bool IsFirstConcatInGroup(Node* node);

  void Analyze();
  void Run();

  JSGraph* jsgraph() { return jsgraph_; }
  Graph* graph() { return jsgraph_->graph(); }
  CommonOperatorBuilder* common() { return jsgraph_->common(); }
  SimplifiedOperatorBuilder* simplified() { return jsgraph_->simplified(); }
  MachineOperatorBuilder* machine() { return jsgraph_->machine(); }
  Schedule* schedule() { return schedule_; }
  Zone* temp_zone() { return temp_zone_; }
  JSHeapBroker* broker() { return broker_; }

 private:
  enum class State : uint8_t {
    kUnvisited = 0,
    kSeenInFindLiterals,
    kSeenInFindPhis,
    kStartConcat,
    kValidConcatPhi,
    kInConcat,
    kConcatEnd,
    kLiteralString,  // A simple literal string
    kValidConcat,    // A concatenation that we can optimize
    kFinalConcat,    // A valid concat that cannot be used for subsequent concat
    kFinishedConcat,  // A ValidConcat that has now been read
    kPending,
    kInvalid,  // Something that we can't optimize
    kNumberOfState
  };

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
      case State::kSeenInFindLiterals:
        return "kSeenInFindLiterals";
      case State::kSeenInFindPhis:
        return "kSeenInFindPhis";
      case State::kStartConcat:
        return "kStartConcat";
      case State::kValidConcatPhi:
        return "kValidConcatPhi";
      case State::kInConcat:
        return "kInConcat";
      case State::kConcatEnd:
        return "kConcatEnd";
      case State::kLiteralString:
        return "kLiteralString";
      case State::kValidConcat:
        return "kValidConcat";
      case State::kFinalConcat:
        return "kFinalConcat";
      case State::kFinishedConcat:
        return "kFinishedConcat";
      case State::kPending:
        return "kPending";
      case State::kInvalid:
        return "kInvalid";
      case State::kNumberOfState:
        return "kNumberOfState";
    }
  }

  bool IsLiteralString(Node* node);

  bool PhiLooksValid(Node* phi);

  NodeVector FindPhis();
  void FindCandidatesFromPhis(NodeVector phis);

  void ReplaceConcatInputIfNeeded(Node* node, int input_idx);

  JSGraph* jsgraph_;
  Schedule* schedule_;
  Zone* temp_zone_;
  JSHeapBroker* broker_;
  ZoneVector<State> state_;  // Cannot use NodeMatcher in reducers
  unsigned int concat_count_ = 0;
  std::unordered_map<Node*, unsigned int> concats_;
  NodeVector concat_starts_;

  NodeVector candidates_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_STRING_BUILDER_OPTIMIZER_H_
