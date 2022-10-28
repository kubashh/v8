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

// StringBuilderOptimizer aims at avoid ConsString for some loops that build
// strings, and instead update a mutable over-allocated backing store, while
// keeping a (mutable) SlicedString to the valid part of the backing store.
//
// StringBuilderOptimizer only does the analysis: it finds out which nodes could
// benefit from this optimization. Then, EffectControlLinearizer actually
// applies the optimization to the graph.
//
// A typical example of what the StringBuilderOptimizer can optimize is:
//
//    let s = "";
//    for (...) {
//        s += "...";
//    }
//
// In general, for a series of concatenations to be optimized, they need:
//   - To start on a single initial concatenation
//   - All the concatenations in the string builder must have constant strings
//     or String.FromCharCode on their right-hand side.
//   - At least one of the concatenation must be in a loop.
//
// Because everything is nicer with a picture, here is one of what kind of
// patterns the StringBuilderOptimizer tries to optimize:
//
//            +--------+
//            |kLiteral|
//            +--------+
//                |
//                |
//                v
//         +-------------+          +--------+
//         |kStringConcat| <------- |kLiteral|
//         +-------------+          +--------+
//                |
//                |
//                v
//           optionally,
//        more kStringConcat
//                |
//                |
//                v
//             +----+
//    -------->|kPhi|------------------------------------------
//    |        +----+                                         |
//    |           |                                           |
//    |           |                                           |
//    |           |                                           |
//    |           |                                           |
//    |           |                                           |
//    |           |                                           |
//    |           |                                           |
//    |           v                                           |
//    |    +-------------+          +--------+                |
//    |    |kStringConcat| <------- |kLiteral|                |
//    |    +-------------+          +--------+                |
//    |           |                                           |
//    |           |                                           |
//    |           v                                           |
//    |      optionally,                                      v
//    |   more kStringConcat                            optionally,
//    |           |                                   more kStringConcat
//    |           |                                   or more kPhi/loops
//    |           |                                           |
//    ------------|                                           |
//                                                            |
//                                                            |
//                                                            v
//
// Where "kLiteral" actually means "either a string literal (HeapConstant) or a
// StringFromSingleCharCode". And kStringConcat can also be kNewConsString (when
// the size of the concatenation is known to be more than 13 bytes, Turbofan's
// front-end generates kNewConsString opcodes rather than kStringConcat).
// The StringBuilder also supports merge phis. For instance:
//
//                                  +--------+
//                                  |kLiteral|
//                                  +--------+
//                                      |
//                                      |
//                                      v
//                               +-------------+          +--------+
//                               |kStringConcat| <------- |kLiteral|
//                               +-------------+          +--------+
//                                  |       |
//                                  |       |
//                                  |       |
//                  +---------------+       +---------------+
//                  |                                       |
//                  |                                       |
//                  v                                       v
//           +-------------+                         +-------------+
//           |kStringConcat|                         |kStringConcat|
//           +-------------+                         +-------------+
//                  |                                       |
//                  |                                       |
//                  |                                       |
//                  +---------------+       +---------------+
//                                  |       |
//                                  |       |
//                                  v       v
//                               +-------------+
//                               |    kPhi     |
//                               |   (merge)   |
//                               +-------------+
//                                      |
//                                      |
//                                      v
//
// (and, of course, loops and merge can be mixed).

class OneOrTwoByteAnalysis final {
  // The class OneOrTwoByteAnalysis is used to try to statically determine
  // whether a string constant or StringFromSingleCharCode is a 1-byte or a
  // 2-byte string.
  // If we succeed to do this analysis for all of the nodes in a string builder,
  // then we know statically whether this string builder is building a 1-byte or
  // a 2-byte string, and we can optimize the generated code to remove all
  // 1-byte/2-byte checks.
 public:
  OneOrTwoByteAnalysis(Graph* graph, Zone* zone, JSHeapBroker* broker)
      : states_(graph->NodeCount(), State::kUnknown, zone), broker_(broker) {}

  enum class State : uint8_t {
    kUnknown,
    kOneByte,  // Only 1-byte strings in the string builder
    kTwoByte,  // At least one 2-byte string in the string builder
    kCantKnow
  };

  // Computes and returns a State reflecting whether {node} is a 1-byte or
  // 2-byte string.
  State OneOrTwoByte(Node* node);

  // Computes whether the string builder will be on 1-byte or 2-byte if it
  // contains two nodes that have states {a} and {b}. For instance, if both {a}
  // and {b} are kOneByte, MergeStates returns kOneByte.
  static State MergeStates(State a, State b);

 private:
  // Returns the positive integral range that {node} can take. If {node} can be
  // negative or is not a number, returns nullopt. If the range exceeds 2**32,
  // returns nullopt as well.
  base::Optional<std::pair<int64_t, int64_t>> TryGetRange(Node* node);

  JSHeapBroker* broker() { return broker_; }

  ZoneVector<State> states_;
  JSHeapBroker* broker_;
};

class V8_EXPORT_PRIVATE StringBuilderOptimizer final {
 public:
  StringBuilderOptimizer(JSGraph* jsgraph, Schedule* schedule, Zone* temp_zone,
                         JSHeapBroker* broker);

  // Returns true if some trimming code should be inserted at the begining of
  // {block} to finalize some string builders.
  bool BlockShouldFinalizeStringBuilders(BasicBlock* block);
  // Returns which nodes should be trimmed at the begining of {block} to
  // finalize some string builders.
  ZoneVector<Node*> GetStringBuildersToFinalize(BasicBlock* block);

  // Returns true if {node} is the last node of a StringBuilder (which means
  // that trimming code should be inserted after {node}).
  bool IsStringBuilderEnd(Node* node);
  // Returns true if {node} is a the last node of a StringBuilder and is not a
  // loop phi. The "loop phi" distinction matters, because trimming for loop
  // phis is trickier (because we don't want to trim at every iteration of the
  // loop, but only once after the loop).
  bool IsNonLoopPhiStringBuilderEnd(Node* node);
  // Returns true if {node} is the input of a concatenation that is part of a
  // StringBuilder.
  bool IsStringBuilderConcatInput(Node* node);
  // Returns true if {node} is part of a StringBuilder.
  bool ConcatIsInStringBuilder(Node* node);
  // Returns true if {node} is the 1st node of a StringBuilder (which means that
  // when lowering {node}, we should allocate and initialize everything for this
  // particular StringBuilder).
  bool IsFirstConcatInStringBuilder(Node* node);

  // Returns a OneOrTwoByteAnalysis::State representing whether the
  // StringBuilder that contains {node} is building a 1-byte or a 2-bvyte
  OneOrTwoByteAnalysis::State GetOneOrTwoByte(Node* node);

  void Run();

  JSGraph* jsgraph() const { return jsgraph_; }
  Graph* graph() const { return jsgraph_->graph(); }
  Schedule* schedule() const { return schedule_; }
  Zone* temp_zone() const { return temp_zone_; }
  JSHeapBroker* broker() const { return broker_; }

 private:
  enum class State : uint8_t {
    kUnvisited = 0,
    kBeginStringBuilder,        // A (potential) begining of a StringBuilder
    kInStringBuilder,           // A node that could be in a StringBuilder
    kPendingPhi,                // A phi that could be in a StringBuilder
    kConfirmedInStringBuilder,  // A node that is definitely in a StringBuilder
    kEndStringBuilder,  // A node that ends definitely a StringBuilder, and that
                        // can be trimmed right away
    kEndStringBuilderLoopPhi,  // A phi that ends a StringBuilder, and whose
                               // trimming need to be done at the begining of
                               // the following blocks.
    kInvalid,  // A node that we visited and that we can't optimize.
    kNumberOfState
  };

  struct Status {
    int id;       // The id of the StringBuilder that the node belongs to (or
                  // kInvalidId).
    State state;  // The state of the node.
  };
  static constexpr int kInvalidId = -1;

  Status GetStatus(Node* node) const {
    if (node->id() > status_.size()) {
      return Status{kInvalidId, State::kInvalid};
    } else {
      return status_[node->id()];
    }
  }
  void SetStatus(Node* node, State state, int id = kInvalidId) {
    DCHECK_NE(state, State::kUnvisited);
    DCHECK_IMPLIES(id != kInvalidId, state != State::kInvalid);
    if (node->id() >= status_.size()) {
      // We should really not allocate too many new nodes: the only new nodes we
      // allocate are constant inputs of nodes in the string builder that have
      // multiple uses. Thus, we don't grow {status_} exponentially, but rather
      // just linearly to save up some memory. "100" should be plenty for most
      // cases, while being small enough to not really cost too much memory in
      // cases where only 1 or 2 would have been enough.
      constexpr int growth_size = 100;
      status_.resize(node->id() + growth_size,
                     Status{kInvalidId, State::kUnvisited});
    }
    status_[node->id()] = Status{id, state};
  }
  void UpdateStatus(Node* node, State state) {
    int id = state == State::kInvalid ? kInvalidId : GetStatus(node).id;
    status_[node->id()] = Status{id, state};
  }

  struct StringBuilder {
    Node* start;
    int id;
    bool has_loop_phi;
    OneOrTwoByteAnalysis::State one_or_two_bytes;
  };
  const StringBuilder kInvalidStringBuilder = {
      nullptr, kInvalidId, false, OneOrTwoByteAnalysis::State::kUnknown};

#ifdef DEBUG
  bool StringBuilderIsValid(StringBuilder string_builder) {
    return string_builder.start != nullptr && string_builder.id != kInvalidId &&
           string_builder.has_loop_phi;
  }
#endif

  bool IsLoopPhi(Node* node) const {
    return node->opcode() == IrOpcode::kPhi &&
           schedule()->block(node)->IsLoopHeader();
  }
  bool LoopContains(Node* loop_phi, Node* node) {
    DCHECK(IsLoopPhi(loop_phi));
    return schedule()->block(loop_phi)->LoopContains(schedule()->block(node));
  }

  int GetStringBuilderIdForConcat(Node* node);
  void ReplaceConcatInputIfNeeded(Node* node, int input_idx);
  bool CheckNodeUses(Node* node, Node* concat_child, Status status);
  bool CheckPreviousNodeUses(Node* child, Status status,
                             int input_if_loop_phi = 0);
  int GetPhiPredecessorsCommonId(Node* node);

  void FinalizeStringBuilders();
  void VisitNode(Node* node, BasicBlock* block);
  void VisitGraph();

  static constexpr bool kAllowAnyStringOnTheRhs = false;

  JSGraph* jsgraph_;
  Schedule* schedule_;
  Zone* temp_zone_;
  JSHeapBroker* broker_;
  unsigned int string_builder_count_ = 0;
  // {blocks_to_trimmings_map_} is a map from block IDs to loop phi nodes that
  // end string builders. For each such node, a trimming should be inserted at
  // the begining of the block (in EffectControlLinearizer) in order to properly
  // finish the string builder (well, things will work if the trimming is
  // omitted, but adding this trimming save memory and removes the SlicedString
  // indirection).
  ZoneVector<base::Optional<ZoneVector<Node*>>> blocks_to_trimmings_map_;
  ZoneVector<Status> status_;
  ZoneVector<StringBuilder> string_builders_;
  // {loop_headers_} is used to keep track ot the start of each loop that the
  // block currently being visited is part of.
  ZoneVector<BasicBlock*> loop_headers_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_STRING_BUILDER_OPTIMIZER_H_
