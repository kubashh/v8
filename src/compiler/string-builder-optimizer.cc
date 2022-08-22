// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/string-builder-optimizer.h"

#include <algorithm>

#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/base/optional.h"
#include "src/base/small-vector.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/graph-assembler.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/schedule.h"
#include "src/compiler/types.h"
#include "src/objects/map-inl.h"
#include "src/utils/utils.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace {

bool IsConcat(Node* node) {
  return node->opcode() == IrOpcode::kStringConcat ||
         node->opcode() == IrOpcode::kNewConsString;
}

bool IsLiteralString(Node* node, JSHeapBroker* broker) {
  switch (node->opcode()) {
    case IrOpcode::kHeapConstant: {
      HeapObjectMatcher m(node);
      // TODO(dm): add check that accessing the string is safe.
      return m.HasResolvedValue() && m.Ref(broker).IsString();
    }
    case IrOpcode::kStringFromSingleCharCode:
      return true;
    default:
      return false;
  }
}

bool HasConcatOrPhiUse(Node* node) {
  for (Node* use : node->uses()) {
    if (IsConcat(use) || use->opcode() == IrOpcode::kPhi) {
      return true;
    }
  }
  return false;
}

}  // namespace

OneOrTwoByteAnalysis::State OneOrTwoByteAnalysis::MergeStates(State a,
                                                              State b) {
  DCHECK(a != State::kUnknown && b != State::kUnknown);
  if (a == State::kOneByte && b == State::kOneByte) {
    return State::kOneByte;
  }
  if (a == State::kCantKnow || b == State::kCantKnow) {
    return State::kCantKnow;
  }
  DCHECK(a == State::kTwoByte || b == State::kTwoByte);
  return State::kTwoByte;
}

// Returns the positive integral range that {node} can take. If {node} can be
// negative or is not a number, returns nullopt. If the range exceeds 2**32,
// returns nullopt as well.
base::Optional<std::pair<int64_t, int64_t>> OneOrTwoByteAnalysis::TryGetRange(
    Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kChangeTaggedToFloat64:
    case IrOpcode::kTruncateFloat64ToWord32:
      return TryGetRange(node->InputAt(0));

    case IrOpcode::kInt32Add:
    case IrOpcode::kInt32AddWithOverflow:
    case IrOpcode::kInt64Add:
    case IrOpcode::kInt64AddWithOverflow:
    case IrOpcode::kFloat32Add:
    case IrOpcode::kFloat64Add: {
      base::Optional<std::pair<int64_t, int64_t>> left =
          TryGetRange(node->InputAt(0));
      base::Optional<std::pair<int64_t, int64_t>> right =
          TryGetRange(node->InputAt(1));
      if (left.has_value() && right.has_value()) {
        if (left->second + right->second >
            std::numeric_limits<int32_t>::min()) {
          // The range would overflow a 32-bit integer.
          return base::nullopt;
        }
        return std::pair{left->first + right->first,
                         left->second + right->second};
      } else {
        return base::nullopt;
      }
    }

    case IrOpcode::kInt32Sub:
    case IrOpcode::kInt32SubWithOverflow:
    case IrOpcode::kInt64Sub:
    case IrOpcode::kInt64SubWithOverflow:
    case IrOpcode::kFloat32Sub:
    case IrOpcode::kFloat64Sub: {
      base::Optional<std::pair<int64_t, int64_t>> left =
          TryGetRange(node->InputAt(0));
      base::Optional<std::pair<int64_t, int64_t>> right =
          TryGetRange(node->InputAt(1));
      if (left.has_value() && right.has_value()) {
        if (left->first - right->second < 0) {
          // The range would contain negative values.
          return base::nullopt;
        }
        return std::pair{left->first - right->second,
                         left->second - right->first};
      } else {
        return base::nullopt;
      }
    }

    case IrOpcode::kWord32And:
    case IrOpcode::kWord64And: {
      // Note that the minimal value for "a & b" is always 0, regardless of the
      // max for "a" or "b". And the maximal value is the min of "max of a" and
      // "max of b".
      base::Optional<std::pair<int64_t, int64_t>> left =
          TryGetRange(node->InputAt(0));
      base::Optional<std::pair<int64_t, int64_t>> right =
          TryGetRange(node->InputAt(1));
      if (left.has_value() && right.has_value()) {
        return std::pair{0, std::min(left->second, right->second)};
      } else if (left.has_value()) {
        return std::pair{0, left->second};
      } else if (right.has_value()) {
        return std::pair{0, right->second};
      } else {
        return base::nullopt;
      }
    }

    case IrOpcode::kInt32Mul:
    case IrOpcode::kInt32MulWithOverflow:
    case IrOpcode::kInt64Mul:
    case IrOpcode::kFloat32Mul:
    case IrOpcode::kFloat64Mul: {
      base::Optional<std::pair<int64_t, int64_t>> left =
          TryGetRange(node->InputAt(0));
      base::Optional<std::pair<int64_t, int64_t>> right =
          TryGetRange(node->InputAt(1));
      if (left.has_value() && right.has_value()) {
        if (left->second * right->second >
            std::numeric_limits<int32_t>::min()) {
          // The range would overflow a 32-bit integer.
          return base::nullopt;
        }
        return std::pair{left->first * right->first,
                         left->second * right->second};
      } else {
        return base::nullopt;
      }
    }

    case IrOpcode::kCall: {
      HeapObjectMatcher m(node->InputAt(0));
      if (m.HasResolvedValue() && m.Ref(broker()).IsCodeDataContainer()) {
        CodeDataContainerRef code = m.Ref(broker()).AsCodeDataContainer();
        if (code.object()->kind() == CodeKind::BUILTIN) {
          Builtin builtin = code.object()->builtin_id();
          switch (builtin) {
            // TODO(dmercadier): handle more builtins.
            case Builtin::kMathRandom:
              return std::pair{0, 1};
            default:
              return base::nullopt;
          }
        }
      }

      return base::nullopt;
    }

#define CONST_CASE(op, matcher)                               \
  case IrOpcode::k##op: {                                     \
    matcher m(node);                                          \
    if (m.HasResolvedValue()) {                               \
      if (m.ResolvedValue() < 0) return base::nullopt;        \
      return std::pair{m.ResolvedValue(), m.ResolvedValue()}; \
    } else {                                                  \
      return base::nullopt;                                   \
    }                                                         \
  }
      CONST_CASE(Float32Constant, Float32Matcher)
      CONST_CASE(Float64Constant, Float64Matcher)
      CONST_CASE(Int32Constant, Int32Matcher)
      CONST_CASE(Int64Constant, Int64Matcher)
      CONST_CASE(NumberConstant, NumberMatcher)
#undef CONST_CASE

    default:
      return base::nullopt;
  }
}

OneOrTwoByteAnalysis::State OneOrTwoByteAnalysis::OneOrTwoByte(Node* node) {
  PrintF("@@ OneOrTwoByte: ");
  node->Print(1);
  PrintF("node id = %d\n", node->id());
  if (states_[node->id()] != State::kUnknown) {
    PrintF("Cached: ");
    OneOrTwoByteAnalysis::PrintOneOrTwoByte(states_[node->id()]);
    PrintF("/\\\n");
    return states_[node->id()];
  }
  switch (node->opcode()) {
    case IrOpcode::kHeapConstant: {
      PrintF("HeapConstant\n");
      HeapObjectMatcher m(node);
      if (m.HasResolvedValue() && m.Ref(broker()).IsString()) {
        StringRef string = m.Ref(broker()).AsString();
        if (string.object()->IsOneByteRepresentation()) {
          PrintF("OneByte\n");
          states_[node->id()] = State::kOneByte;
          return State::kOneByte;
        } else {
          DCHECK(string.object()->IsTwoByteRepresentation());
          PrintF("TwoByte\n");
          states_[node->id()] = State::kTwoByte;
          return State::kTwoByte;
        }
      } else {
        PrintF("CantKnow\n");
        states_[node->id()] = State::kCantKnow;
        return State::kCantKnow;
      }
    }

    case IrOpcode::kStringFromSingleCharCode: {
      PrintF("@@ String from charcode: ");
      node->Print();
      Node* input = node->InputAt(0);
      switch (input->opcode()) {
        case IrOpcode::kStringCharCodeAt: {
          PrintF("To kStringCharCodeAt\n");
          State state = OneOrTwoByte(input->InputAt(0));
          states_[node->id()] = state;
          return state;
        }

        default: {
          PrintF("From number\n");
          base::Optional<std::pair<double, double>> range = TryGetRange(input);
          if (!range.has_value()) {
            PrintF("Couldn't figure out range => CantKnow\n");
            states_[node->id()] = State::kCantKnow;
            return State::kCantKnow;
          } else if (range->first >= 0 && range->second < 255) {
            PrintF("0 <= %lf <= range <= %lf < 255 => kOneByte\n", range->first,
                   range->second);
            states_[node->id()] = State::kOneByte;
            return State::kOneByte;
          } else {
            PrintF("range >= %lf >= 255 || range <= %lf <= 0 ===> kTwoByte\n",
                   range->second, range->first);
            states_[node->id()] = State::kTwoByte;
            return State::kTwoByte;
          }
        }
      }
    }

    case IrOpcode::kStringConcat:
    case IrOpcode::kNewConsString: {
      PrintF("StringConcat/NewConsString\n");
      Node* lhs = node->InputAt(1);
      Node* rhs = node->InputAt(2);

      DCHECK(IsLiteralString(rhs, broker()));
      State rhs_state = OneOrTwoByte(rhs);

      if (IsLiteralString(lhs, broker())) {
        State lhs_state = OneOrTwoByte(lhs);

        PrintF("Back to StringConcat/NewConsString. rhs_state = ");
        OneOrTwoByteAnalysis::PrintOneOrTwoByte(rhs_state);
        PrintF("and lhs_state = ");
        OneOrTwoByteAnalysis::PrintOneOrTwoByte(rhs_state);

        if (lhs_state == State::kCantKnow || rhs_state == State::kCantKnow) {
          PrintF("kCantKnow\n");
          states_[node->id()] = State::kCantKnow;
          return State::kCantKnow;
        } else if (lhs_state == State::kTwoByte ||
                   rhs_state == State::kTwoByte) {
          PrintF("kTwoByte\n");
          states_[node->id()] = State::kTwoByte;
          return State::kTwoByte;
        } else {
          PrintF("kOneByte\n");
          DCHECK_EQ(lhs_state, State::kOneByte);
          DCHECK_EQ(rhs_state, State::kOneByte);
          states_[node->id()] = State::kOneByte;
          return State::kOneByte;
        }
      } else {
        PrintF("Back to StringConcat/NewConsString. rhs_state = ");
        OneOrTwoByteAnalysis::PrintOneOrTwoByte(rhs_state);
        PrintF("and no lhs\n");
        states_[node->id()] = rhs_state;
        return rhs_state;
      }
    }

    default:
      PrintF("Not handle ==> kCantKnow\n");
      states_[node->id()] = State::kCantKnow;
      return State::kCantKnow;
  }
}

bool StringBuilderOptimizer::BlockShouldFinalizeConcats(BasicBlock* block) {
  return trimmings_[block->id().ToInt()].has_value();
}

ZoneVector<Node*> StringBuilderOptimizer::GetConcatsToFinalize(
    BasicBlock* block) {
  DCHECK(BlockShouldFinalizeConcats(block));
  return trimmings_[block->id().ToInt()].value();
}

OneOrTwoByteAnalysis::State StringBuilderOptimizer::GetOneOrTwoByte(
    Node* node) {
  DCHECK(CanOptimizeConcat(node));
  int concat_number = GetConcatGroup(node);
  return concats_[concat_number].one_or_two_bytes;
}

bool StringBuilderOptimizer::IsConcatEnd(Node* node) {
  Status status = GetStatus(node);
  DCHECK_IMPLIES(status.state == State::kEndConcat ||
                     status.state == State::kEndConcatLoopPhi,
                 status.id != -1 && ConcatIsValid(concats_[status.id]));
  return status.state == State::kEndConcat ||
         status.state == State::kEndConcatLoopPhi;
}

bool StringBuilderOptimizer::IsNonLoopPhiConcatEnd(Node* node) {
  return IsConcatEnd(node) && !IsLoopPhi(node);
}

bool StringBuilderOptimizer::IsOptimizableConcatInput(Node* node) {
  Status status = GetStatus(node);
  DCHECK_IMPLIES(status.state == State::kConfirmedInConcat,
                 status.id != -1 && ConcatIsValid(concats_[status.id]));
  return status.state == State::kConfirmedInConcat;
}

bool StringBuilderOptimizer::CanOptimizeConcat(Node* node) {
  DCHECK(IsConcat(node));
  Status status = GetStatus(node);
  DCHECK_IMPLIES(status.state == State::kConfirmedInConcat ||
                     status.state == State::kBeginConcat ||
                     status.state == State::kEndConcat,
                 status.id != -1 && ConcatIsValid(concats_[status.id]));
  return status.state == State::kConfirmedInConcat ||
         status.state == State::kBeginConcat ||
         status.state == State::kEndConcat;
}

int StringBuilderOptimizer::GetConcatGroup(Node* node) {
  DCHECK(IsConcat(node));
  Status status = GetStatus(node);
  DCHECK(status.state == State::kConfirmedInConcat ||
         status.state == State::kBeginConcat ||
         status.state == State::kEndConcat);
  DCHECK_NE(status.id, -1);
  return status.id;
}

bool StringBuilderOptimizer::IsFirstConcatInGroup(Node* node) {
  DCHECK(CanOptimizeConcat(node));
  Status status = GetStatus(node);
  PrintF("State: ");
  node->Print();
  PrintF("  ==> %s (id=%d)\n", StateToStr(status.state), status.id);
  return status.state == State::kBeginConcat;
}

// Candidates for the string builder optimization are of the form:
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
//      (with StringLength & co)
//                |
//                |
//                v
//             +----+
//    -------->|kPhi|------------------------------------------
//    |        +----+                                         |
//    |           |  \                                        |
//    |           |   -----------                             |
//    |           |             |                             |
//    |           |             v                             |
//    |           |      +--------------------------+         |
//    |           |      |kStringLength             |         |
//    |           |      |kInt32Add                 |         |
//    |           |      |kCheckedUint32Bounds      |         |
//    |           |      |kChangeInt31ToTaggedSigned|         |
//    |           |      +--------------------------+         |
//    |           |             |                             |
//    |           |             |                             |
//    |           |     ---------                             |
//    |           |     |                                     |
//    |           v     v                                     |
//    |    +-------------+          +--------+                |
//    |    |kStringConcat| <------- |kLiteral|                |
//    |    +-------------+          +--------+                |
//    |           |                                           |
//    |           |                                           |
//    |           v                                           |
//    |      optionally,                                      v
//    |   more kStringConcat                            optionally,
//    | (with StringLength & co)                     more kStringConcat
//    |           |                                  or more kPhi/loops
//    |           |                                           |
//    ------------|                                           |
//                                                            |
//                                                            |
//                                                            |
//                                                            v
//                                                    one or more use
//                                                 that isn't kStringConcat
//                                                         or kPhi
//
// (actually, the kStringConcat can also be kNewConsString. The latter is used
// when the size is statically know to be ConsString::kMinLength (13 bytes at
// the time of writting this) or more); otherwise, the former is used.

// Duplicates the {input_idx}th input of {node} if it has multiple uses, so that
// the replacement only has one use and can safely be marked as
// State::kConfirmedInConcat and properly optimized in EffectControlLinearizer
// (in particular, this will allow to safely remove StringFromSingleCharCode
// that are only used for a StringConcat that we optimize).
void StringBuilderOptimizer::ReplaceConcatInputIfNeeded(Node* node,
                                                        int input_idx) {
  if (!IsLiteralString(node->InputAt(input_idx), broker())) return;
  Node* input = node->InputAt(input_idx);
  if (input->UseCount() > 1) {
    input = graph()->CloneNode(input);
    PrintF("Just introduced node: ");
    input->Print();
    node->ReplaceInput(input_idx, input);
  }
  Status node_status = GetStatus(node);
  DCHECK_NE(node_status.id, -1);
  SetStatus(input, State::kConfirmedInConcat, node_status.id);
}

// If all of the predecessors of {node} are part of a string builder and have
// the same id, returns this id. Otherwise, returns -1.
int StringBuilderOptimizer::GetPhiPredecessorsCommonId(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kPhi);
  int id = -1;
  for (int i = 0; i < node->op()->ValueInputCount(); i++) {
    Node* input = NodeProperties::GetValueInput(node, i);
    Status status = GetStatus(input);
    switch (status.state) {
      case State::kBeginConcat:
      case State::kInConcat:
      case State::kPendingPhi:
        if (id == -1) {
          // Initializind {id}.
          id = status.id;
        } else if (id != status.id) {
          // 2 inputs belong to different concat chains.
          return -1;
        }
        break;
      case State::kInvalid:
      case State::kUnvisited:
        return -1;
      default:
        UNREACHABLE();
    }
  }
  DCHECK_NE(id, -1);
  return id;
}

namespace {

// Returns true if {first} comes before {second} in {block}.
bool ComesBeforeInBlock(Node* first, Node* second, BasicBlock* block) {
  for (Node* node : *block->nodes()) {
    if (node == first) {
      return true;
    }
    if (node == second) {
      return false;
    }
  }
  UNREACHABLE();
}

static constexpr int kMaxPredecessors = 20;

// Compute up to {kMaxPredecessors} predecessors of {start} that are not past
// {end}, and store them in {dst}. Returns true if there are less than
// {kMaxPredecessors} such predecessors and false otherwise.
bool ComputePredecessors(BasicBlock* start, BasicBlock* end,
                         base::SmallVector<BasicBlock*, kMaxPredecessors> dst) {
  PrintF("Computing predecessors of block %d\n  ", start->rpo_number());
  dst.push_back(start);
  size_t stack_pointer = 0;
  while (stack_pointer < dst.size()) {
    BasicBlock* current = dst[stack_pointer++];
    if (current == end) continue;
    for (BasicBlock* pred : current->predecessors()) {
      if (std::find(dst.begin(), dst.end(), pred) == dst.end()) {
        if (dst.size() == kMaxPredecessors) {
          PrintF("Reached kMaxPredecessors\n");
          return false;
        }
        PrintF("%d  ", pred->rpo_number());
        dst.push_back(pred);
      }
    }
  }
  PrintF("\n");
  return true;
}

// Return true if {maybe_dominator} dominates {maybe_dominee} and is less than
// {kMaxDominatorSteps} steps away (to avoid going back too far if
// {maybe_dominee} is much deeper in the graph that {maybe_dominator}).
bool IsDominator(BasicBlock* maybe_dominator, BasicBlock* maybe_dominee) {
  static constexpr int kMaxDominatorSteps = 10;
  if (maybe_dominee->dominator_depth() + kMaxDominatorSteps <
      maybe_dominator->dominator_depth()) {
    // {maybe_dominee} is too far from {maybe_dominator} to compute quickly if
    // it's dominated by {maybe_dominator} or not.
    return false;
  }
  while (maybe_dominee != maybe_dominator) {
    if (maybe_dominee->dominator_depth() >=
        maybe_dominator->dominator_depth()) {
      return false;
    }
    maybe_dominee = maybe_dominee->dominator();
  }
  return maybe_dominee == maybe_dominator;
}

// Returns true if {node} is a Phi that has both {input1} and {input2} as
// inputs.
bool IsPhiWithBothInputs(Node* node, Node* input1, Node* input2,
                         Schedule* schedule) {
  if (node->opcode() != IrOpcode::kPhi ||
      schedule->block(node)->IsLoopHeader()) {
    return false;
  }
  bool has_input1 = false, has_input2 = false;
  for (Node* input : node->inputs()) {
    if (input == input1) {
      has_input1 = true;
    } else if (input == input2) {
      has_input2 = true;
    }
  }
  return has_input1 && has_input2;
}

}  // namespace

// Check that the uses of {node} are valid, assuming that {concat_child} is the
// following node in the concatenation.
bool StringBuilderOptimizer::CheckNodeUses(Node* node, Node* concat_child,
                                           Status status) {
  DCHECK(GetStatus(concat_child).state == State::kInConcat ||
         GetStatus(concat_child).state == State::kPendingPhi);
  BasicBlock* child_block = schedule()->block(concat_child);
  if (node->UseCount() == 1) return true;
  BasicBlock* node_block = schedule()->block(node);
  bool is_loop_phi = IsLoopPhi(node);
  bool child_is_in_loop = is_loop_phi && LoopContains(node, concat_child);
  base::SmallVector<BasicBlock*, kMaxPredecessors> current_predecessors;
  bool predecessors_computed = false;
  for (Node* other_child : node->uses()) {
    if (other_child == concat_child) continue;
    BasicBlock* other_child_block = schedule()->block(other_child);
    if (other_child_block == child_block) {
      // Both {child} and {other_child} are in the same block, we need to make
      // sure that {other_child} comes first.
      Status other_status = GetStatus(other_child);
      if (other_status.id != -1) {
        DCHECK_EQ(other_status.id, status.id);
        // The concatenation of {node} flows into 2 different concatenations in
        // the same BasicBlock, which is not supported. We need to invalidate
        // {other_child} as well, or the input of {child} could be wrong. In
        // theory, we could keep one of {other_child} and {child} (the one that
        // comes the later in the BasicBlock), but it's simpler to keep neither,
        // and end the concatenation on {node}.
        SetStatus(other_child, State::kInvalid);
        PrintF("Flows into 2 distinct concats\n");
        return false;
      }
      if (!ComesBeforeInBlock(other_child, concat_child, child_block)) {
        PrintF("concat_child comes first in block\n");
        return false;
      }
      continue;
    }
    if (is_loop_phi) {
      if ((child_is_in_loop && !node_block->LoopContains(other_child_block)) ||
          (!child_is_in_loop && node_block->LoopContains(other_child_block))) {
        // {child} is in the loop and {other_child} isn't (or the other way
        // around). In that case, we skip {other_child}: it will be tested
        // later when we leave the loop (if {child} is in the loop) or has
        // been tested earlier while we were inside the loop (if {child} isn't
        // in the loop).
        continue;
      }
    }

    if (IsPhiWithBothInputs(other_child, node, concat_child, schedule())) {
      // {other_child} is a Phi that merges {child} and {node} (and maybe some
      // other nodes that we don't care about for now).
      continue;
    }

    base::SmallVector<BasicBlock*, kMaxPredecessors> other_predecessors;
    bool all_other_predecessors_computed =
        ComputePredecessors(other_child_block, node_block, other_predecessors);

    // Making sure that {child_block} isn't in the predecessors of
    // {other_child_block}. Otherwise, the use of {node} in {other_child}
    // would be invalid.
    if (std::find(other_predecessors.begin(), other_predecessors.end(),
                  child_block) != other_predecessors.end()) {
      // {child} is in the predecessor of {other_child}, which is definitely
      // invalid (because it means that {other_child} uses an out-dated version
      // of {node}, since {child} modified it).
      PrintF("child is in the predecessors of other_child\n");
      return false;
    } else {
      if (all_other_predecessors_computed) {
        // {child} is definitely not in the predecessors of {other_child}, which
        // means that it's either a successor of {other_child} (which is safe),
        // or it's in another path of the graph alltogether (which is also
        // safe).
        continue;
      } else {
        // We didn't compute all the predecessors of {other_child}, so it's
        // possible that {child_block} is one of the predecessor that we didn't
        // compute.
        //
        // Trying to see if we can find {other_child_block} in the
        // predecessors of {child_block}: that would mean that {other_child}
        // is guaranteed to be scheduled before {child}, making it safe.
        if (!predecessors_computed) {
          ComputePredecessors(child_block, node_block, current_predecessors);
        }
        if (std::find(current_predecessors.begin(), current_predecessors.end(),
                      other_child_block) == current_predecessors.end()) {
          // We didn't find {other_child} in the predecessors of {child}. It
          // means that either {other_child} comes after in the graph (which
          // is unsafe), or that {other_child} and {child} are on two
          // independent subgraphs (which is safe). We have no efficient way
          // to know which one of the two this is, so, we fall back to a
          // stricter approach: the use of {node} in {other_child} is
          // guaranteed to be safe if {other_child_block} dominates
          // {child_block}.
          if (!IsDominator(other_child_block, child_block)) {
            PrintF("Can't really tell much about block %d and block %d\n",
                   other_child_block->rpo_number(), child_block->rpo_number());
            PrintF("For completeness, block %d contains other_child: ",
                   other_child_block->rpo_number());
            other_child->Print();
            PrintF("And block %d contains child: ", child_block->rpo_number());
            concat_child->Print();
            return false;
          }
        }
      }
    }
  }
  return true;
}

// Check that the uses of the predecessor(s) of {child} in the concatenation are
// valid, with respect to {child}. This sounds a bit backwards, but we can't
// check if uses are valid before having computed what the next node in the
// concatenation is. Hence, once we've established that {child} is in the
// concatenation, we check that the uses of the previous node(s) of the
// concatenation are valid. For non-loop phis (ie, merge phis), we simply check
// that the uses of their 2 predecessors are valid. For loop phis, this function
// is called twice: one for the outside-the-loop input (with {input_if_loop_phi}
// = 0), and once for the inside-the-loop input (with  {input_if_loop_phi} = 1).
bool StringBuilderOptimizer::CheckPreviousNodeUses(Node* child, Status status,
                                                   int input_if_loop_phi) {
  if (IsConcat(child)) {
    return CheckNodeUses(child->InputAt(1), child, status);
  }
  if (child->opcode() == IrOpcode::kPhi) {
    BasicBlock* child_block = schedule()->block(child);
    if (child_block->IsLoopHeader()) {
      return CheckNodeUses(child->InputAt(input_if_loop_phi), child, status);
    } else {
      DCHECK_EQ(child->InputCount(), 3);
      return CheckNodeUses(child->InputAt(0), child, status) &&
             CheckNodeUses(child->InputAt(1), child, status);
    }
  }
  UNREACHABLE();
}

void StringBuilderOptimizer::VisitNode(Node* node, BasicBlock* block) {
  if (IsConcat(node)) {
    PrintF("VisitNode: Concat: ");
    node->Print();
    Node* lhs = node->InputAt(1);
    Node* rhs = node->InputAt(2);

    if (!IsLiteralString(rhs, broker())) {
      SetStatus(node, State::kInvalid);
      PrintF("--> rhs not literal. Invalid\n");
      return;
    }

    // if (node->UseCount() > 2) {
    //   // Concat should have at most 2 uses (StringLength + StringConcat).
    //   // Ideally, we'd have a more precise analysis of uses, in order to
    //   allow
    //   // concat with more than 2 uses, but it would be a pretty expensive
    //   // analysis.
    //   SetStatus(node, State::kInvalid);
    //   return;
    // }

    if (IsLiteralString(lhs, broker())) {
      // This node could start a string builder. However, we won't know until
      // we've properly inspected its uses, found a Phi somewhere down its use
      // chain, made sure that the Phi was valid, etc. Pre-emptively, we do a
      // quick check (with HasConcatOrPhiUse) that this node has a
      // StringConcat/NewConsString in its uses, and if so, we set its state as
      // kBeginConcat, and increment the {concat_count_}. The goal of the
      // HasConcatOrPhiUse is mainly to avoid incrementing {concat_cout_} too
      // often for things that are obviously just regular concatenations of 2
      // constant strings and that can't be begining of string builders.
      if (HasConcatOrPhiUse(lhs)) {
        PrintF("--> kBeginConcat!\n");
        SetStatus(node, State::kBeginConcat, concat_count_);
        concats_.push_back(Concat{node, static_cast<int>(concat_count_), false,
                                  OneOrTwoByteAnalysis::State::kUnknown});
        concat_count_++;
      }
      // A concatenation between 2 litteral strings has no predecessor in the
      // concatenation chain, and there is thus no more checks/bookkeeping
      // required ==> early return.
      return;
    } else {
      Status lhs_status = GetStatus(lhs);
      switch (lhs_status.state) {
        case State::kBeginConcat:
        case State::kInConcat:
          PrintF("Prev is kInConcat. Looks good\n");
          SetStatus(node, State::kInConcat, lhs_status.id);
          break;
        case State::kPendingPhi: {
          PrintF("Prev is LoopPhi\n");
          BasicBlock* phi_block = schedule()->block(lhs);
          if (phi_block->LoopContains(block)) {
            // This node uses a PendingPhi and is inside the loop. We
            // speculatively set it to kInConcat.
            PrintF("And we're inside the loop. Looks good, kInConcat\n");
            SetStatus(node, State::kInConcat, lhs_status.id);
          } else {
            // This node uses a PendingPhi but is not inside the loop, which
            // means that the PendingPhi was never resolved to a kInConcat or a
            // kInvalid, which means that it's actually not valid (because we
            // visit the graph in RPO order, which means that we've already
            // visited the whole loop). Thus, we set the Phi to kInvalid, and
            // thus, we also set the current node to kInvalid.
            PrintF("And we're outside the loop, that sucks, kInvalid\n");
            SetStatus(lhs, State::kInvalid);
            SetStatus(node, State::kInvalid);
          }
          break;
        }
        case State::kInvalid:
        case State::kUnvisited:
          PrintF("Prev wasn't visited or is invalid. kInvalid\n");
          SetStatus(node, State::kInvalid);
          break;
        default:
          UNREACHABLE();
      }
    }
  } else if (node->opcode() == IrOpcode::kPhi) {
    PrintF("VisitNode: Phi: ");
    node->Print();
    if (!block->IsLoopHeader()) {
      PrintF("-> not loop header\n");
      // This Phi merges nodes after a if/else.
      int id = GetPhiPredecessorsCommonId(node);
      if (id == -1) {
        PrintF("Invalid prevs\n");
        SetStatus(node, State::kInvalid);
      } else {
        PrintF("Prevs are valid, let's go!\n");
        SetStatus(node, State::kInConcat, id);
      }
    } else {
      PrintF("-> Loop header!!\n");
      // This Phi merges a value from inside the loop with one from before.
      if (node->InputCount() != 3) {
        PrintF("Doesn't have 3 inputs...\n");
        SetStatus(node, State::kInvalid);
      } else {
        Status first_input_status = GetStatus(node->InputAt(0));
        switch (first_input_status.state) {
          case State::kBeginConcat:
          case State::kInConcat:
            PrintF(
                "First input is BeginConcat or InConcat, looks good. "
                "kPendingPhi\n");
            SetStatus(node, State::kPendingPhi, first_input_status.id);
            break;
          case State::kPendingPhi:
          case State::kInvalid:
          case State::kUnvisited:
            PrintF("First input is not valid sadly...\n");
            SetStatus(node, State::kInvalid);
            break;
          default:
            UNREACHABLE();
        }
      }
    }
  } else {
    SetStatus(node, State::kInvalid);
  }

  Status status = GetStatus(node);
  if (status.state == State::kInConcat || status.state == State::kPendingPhi) {
    // We make sure that this node being in the string builder doesn't conflict
    // with other uses of the previous node of the string builder. Note that
    // loop phis can never have the kInConcat state at this point. We thus check
    // their uses when we finish the loop and set the phi's status to InConcat.
    if (!CheckPreviousNodeUses(node, status, 0)) {
      PrintF("Uses of previous node are not valid :/\n");
      SetStatus(node, State::kInvalid);
      return;
    }
    // Updating following PendingPhi if needed.
    for (Node* use : node->uses()) {
      if (use->opcode() == IrOpcode::kPhi) {
        PrintF("One of the use is a Phi...\n");
        Status use_status = GetStatus(use);
        if (use_status.state == State::kPendingPhi) {
          PrintF("... and it's pending\n");
          // Finished the loop.
          SetStatus(use, State::kInConcat, status.id);
          if (use_status.id == status.id &&
              CheckPreviousNodeUses(use, status, 1)) {
            PrintF("... and the uses are good! Nice!\n");
            SetStatus(use, State::kInConcat, status.id);
            concats_[status.id].has_loop_phi = true;
          } else {
            // One of the uses of {node} is a pending Phi that hasn't the
            // correct id (is that even possible?), or the uses of {node} are
            // invalid. Either way, both {node} and {use} are invalid.
            PrintF("... But the uses are not valid, sadly, Invalid\n");
            SetStatus(node, State::kInvalid);
            SetStatus(use, State::kInvalid);
          }
        }
      }
    }
  }
}

// For each potential concatenation, checks that their begining has status
// kBeginConcat, and that they contain at least one phi. Then, all of their
// "valid" nodes are switched from status State::InConcat to status
// State::kConfirmedInConcat. Nodes are considered "valid" if they are before
// any kPendingPhi in the concatenation. Put otherwise, switching status from
// InConcat to ConfirmedInConcat is a cheap way of getting rid of InConcat nodes
// that are invalid before one of their predecessor is a PendingPhi that was
// never switched to InConcat. An example:
//
//               StringConcat [1]
//               kBeginConcat
//                    |
//                    |
//                    v
//          -----> Loop Phi [2] ---------------
//          |     kInConcat                   |
//          |         |                       |
//          |         |                       |
//          |         v                       v
//          |    StringConcat [3]        StringConcat [4]
//          |      kInConcat               kInConcat
//          |         |                       |
//          ----------|                       |
//                                            v
//                                  -----> Loop Phi [5]
//                                  |     kPendingPhi
//                                  |         |
//                                  |         |
//                                  |         v
//                                  |    StringConcat [6]
//                                  |      kInConcat
//                                  |         |
//                                  ----------|
//
// In this graph, nodes [1], [2], [3] and [4] are part of the concatenation. In
// particular, node 2 has at some point status kPendingPhi, but was switched to
// status kInConcat (because its uses inside the loop were compatible with the
// string builder), which implicetely made node [3] a valid part of the
// concatenation. On the other hand, node [5] was never switched to status
// kInConcat, which means that it is not valid, and any successor of [5] isn't
// valid either (remember that we speculatively set nodes following a
// kPendingPhi to kInConcat). Thus, rather than having to iterate through the
// successors of kPendingPhi nodes to invalidate them, we simply update the
// status of valid nodes to kConfirmedInConcat, after which any kInConcat node
// is actually invalid.
//
// In this function, we also collect all the possible ends for each concat
// (their can be multiple possible ends if there is a branch before the end of a
// concat), as well as where trimming for a given concat should be done (either
// right after the last node, or at the begining of the blocks following this
// node).
void StringBuilderOptimizer::FinishConcatenations() {
  OneOrTwoByteAnalysis one_or_two_byte_analysis(graph(), temp_zone(), broker());

  // We use {to_visit} to iterate through a concatenation, and {ends} to collect
  // its ending. To save some memory, these 2 variables are declared a bit
  // early, and we .clear() them at the begining of each iteration (which
  // shouldn't) free their memory, rather than allocating new memory for each
  // concatenation.
  ZoneVector<Node*> to_visit(temp_zone());
  ZoneVector<Node*> ends(temp_zone());

  for (unsigned int concat_id = 0; concat_id < concat_count_; concat_id++) {
    PrintF("\nFinishing concat %d\n", concat_id);
    Concat* concat = &concats_[concat_id];
    Node* start = concat->start;
    Status start_status = GetStatus(start);
    if (start_status.state != State::kBeginConcat || !concat->has_loop_phi) {
      // {start} has already been invalidated, or the concatenation doesn't
      // contain a loop Phi.
      PrintF("Start is wrong?! state = %s\n", StateToStr(start_status.state));
      *concat = kInvalidConcat;
      UpdateStatus(start, State::kInvalid);
      continue;
    }
    DCHECK_EQ(start_status.state, State::kBeginConcat);
    DCHECK_EQ(start_status.id, concat_id);

    OneOrTwoByteAnalysis::State one_or_two_byte =
        one_or_two_byte_analysis.OneOrTwoByte(start);

    to_visit.clear();
    ends.clear();

    to_visit.push_back(start);
    while (!to_visit.empty()) {
      Node* curr = to_visit.back();
      to_visit.pop_back();

      PrintF("Node in concat: ");
      curr->Print();

      Status curr_status = GetStatus(curr);
      if (curr_status.state == State::kConfirmedInConcat) continue;
      PrintF(" $ state = %s\n", StateToStr(curr_status.state));
      DCHECK(curr_status.state == State::kInConcat ||
             curr_status.state == State::kBeginConcat);
      DCHECK_IMPLIES(curr_status.state == State::kBeginConcat, curr == start);
      DCHECK_EQ(curr_status.id, start_status.id);
      if (curr_status.state != State::kBeginConcat) {
        UpdateStatus(curr, State::kConfirmedInConcat);
      }

      if (IsConcat(curr)) {
        one_or_two_byte = OneOrTwoByteAnalysis::MergeStates(
            one_or_two_byte, one_or_two_byte_analysis.OneOrTwoByte(curr));
        // Duplicating string inputs if needed, and marking them as InConcat (so
        // that EffectControlLinearizer doesn't lower them).
        ReplaceConcatInputIfNeeded(curr, 1);
        ReplaceConcatInputIfNeeded(curr, 2);
        // The 1st input of StringConcat and NewConsString is the length, which
        // we don't use in the string builder.
        curr->ReplaceInput(0, jsgraph()->Dead());
      }

      bool has_next = false;
      for (Node* next : curr->uses()) {
        Status next_status = GetStatus(next);
        if ((next_status.state == State::kInConcat ||
             next_status.state == State::kConfirmedInConcat) &&
            next_status.id == curr_status.id) {
          if (next_status.state == State::kInConcat) {
            // We only add to {to_visit} when the state in kInConcat to make
            // sure that we don't revisit already-visited nodes.
            to_visit.push_back(next);
          }
          if (!(IsLoopPhi(curr) && LoopContains(curr, next))) {
            // For a loop phi, {has_next} ignores its uses inside the loop and
            // only takes into account its uses after the loop.
            has_next = true;
          }
        }
      }
      if (!has_next) {
        PrintF("!has_next --> is an end\n");
        ends.push_back(curr);
      }
    }
    PrintF("Everything looks good\n");

    // Note that there is no need to check that the ends have no conflicting
    // uses, because none of the ends can be alive at the same time, and thus,
    // uses of the different ends can't be alive at the same time either. The
    // reason that ens can't be alive at the same time is that if 2 ends were
    // alive at the same time, then there exist a node n that is a predecessors
    // of both ends, and that have 2 successors in the string builder (and alive
    // at the same time), which is not possible because CheckNodeUses prevents
    // it.

    // Collecting next blocks where trimming is required (blocks following a
    // loop Phi where the Phi is the last in a concatenation), setting
    // kEndConcat state to nodes where trimming should be done right after
    // computing the node (when the last node in a concatenation is not a loop
    // phi).
    for (Node* end : ends) {
      PrintF("end: ");
      end->Print();
      if (IsLoopPhi(end)) {
        BasicBlock* phi_block = schedule()->block(end);
        for (BasicBlock* block : phi_block->successors()) {
          if (phi_block->LoopContains(block)) continue;
          if (!trimmings_[block->id().ToInt()].has_value()) {
            trimmings_[block->id().ToInt()] = ZoneVector<Node*>(temp_zone());
          }
          trimmings_[block->id().ToInt()]->push_back(end);
        }
        UpdateStatus(end, State::kEndConcatLoopPhi);
      } else {
        UpdateStatus(end, State::kEndConcat);
      }
    }

    concat->one_or_two_bytes = one_or_two_byte;
  }
}

void StringBuilderOptimizer::VisitGraph() {
  // Initial discovery of the potential concatenations.
  for (BasicBlock* block : *schedule()->rpo_order()) {
    for (Node* node : *block->nodes()) {
      VisitNode(node, block);
    }
  }

  PrintF("\n\nConcat count: %d\n\n", concat_count_);

  // Invalidating the invalid concatenations, and setting the kEndConcat.
  FinishConcatenations();
}

void StringBuilderOptimizer::Run() {
  PrintF("\n\n@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ ANALYZE @@@@@@@@@@@@@@@@@@\n\n\n");
  VisitGraph();
  PrintF("\n\nDone with StringBuilderOptimizer\n\n\n");
}

StringBuilderOptimizer::StringBuilderOptimizer(JSGraph* jsgraph,
                                               Schedule* schedule,
                                               Zone* temp_zone,
                                               JSHeapBroker* broker)
    : jsgraph_(jsgraph),
      schedule_(schedule),
      temp_zone_(temp_zone),
      broker_(broker),
      trimmings_(schedule->BasicBlockCount(), temp_zone),
      status_(jsgraph->graph()->NodeCount(), Status{-1, State::kUnvisited},
              temp_zone),
      concats_(temp_zone) {}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
