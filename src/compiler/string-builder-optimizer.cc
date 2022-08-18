// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/string-builder-optimizer.h"

#include <algorithm>

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
  if (a == State::kOneByte && b == State::kOneByte) {
    return State::kOneByte;
  }
  if (a == State::kCantKnow || b == State::kCantKnow) {
    return State::kCantKnow;
  }
  if (a == State::kUnknown && b == State::kUnknown) {
    return State::kUnknown;
  }
  DCHECK(a == State::kTwoByte || b == State::kTwoByte);
  return State::kTwoByte;
}

base::Optional<double> OneOrTwoByteAnalysis::TryGetRange(Node* node) {
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
      base::Optional<double> left = TryGetRange(node->InputAt(0));
      base::Optional<double> right = TryGetRange(node->InputAt(1));
      if (left.has_value() && right.has_value()) {
        return *left + *right;
      } else {
        return base::nullopt;
      }
    }

    case IrOpcode::kInt32Mul:
    case IrOpcode::kInt32MulWithOverflow:
    case IrOpcode::kInt64Mul:
    case IrOpcode::kFloat32Mul:
    case IrOpcode::kFloat64Mul: {
      base::Optional<double> left = TryGetRange(node->InputAt(0));
      base::Optional<double> right = TryGetRange(node->InputAt(1));
      if (left.has_value() && right.has_value()) {
        return *left * *right;
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
              return 1;
            default:
              return base::nullopt;
          }
        }
      }

      return base::nullopt;
    }

#define CONST_CASE(op, matcher) \
  case IrOpcode::k##op: {       \
    matcher m(node);            \
    if (m.HasResolvedValue()) { \
      return m.ResolvedValue(); \
    } else {                    \
      return base::nullopt;     \
    }                           \
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
          base::Optional<double> max_range = TryGetRange(input);
          if (!max_range.has_value()) {
            PrintF("Couldn't figure out range => CantKnow\n");
            states_[node->id()] = State::kCantKnow;
            return State::kCantKnow;
          } else if (*max_range < 255) {
            PrintF("range < 255 (%lf) => kOneByte\n", *max_range);
            states_[node->id()] = State::kOneByte;
            return State::kOneByte;
          } else {
            PrintF("range >= 255 (%lf) => kTwoByte\n", *max_range);
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
  return reverse_trimmings[block->id().ToInt()].has_value();
}

ZoneVector<Node*> StringBuilderOptimizer::GetConcatsToFinalize(
    BasicBlock* block) {
  DCHECK(BlockShouldFinalizeConcats(block));
  return reverse_trimmings[block->id().ToInt()].value();
}

OneOrTwoByteAnalysis::State StringBuilderOptimizer::GetOneOrTwoByte(
    Node* node) {
  DCHECK(CanOptimizeConcat(node));
  int concat_number = GetConcatGroup(node);
  return concat_one_or_two_bytes_[concat_number];
}

// bool StringBuilderOptimizer::IsConcatEnd(Node* node) {
//   return state_[node->id()] == State::kConcatEnd;
// }
bool StringBuilderOptimizer::IsConcatEnd(Node* node) {
  Status status = GetStatus(node);
  return status.state == State::kEndConcat && status.id != -1 &&
         concat_starts_[status.id] != nullptr;
}

// bool StringBuilderOptimizer::IsOptimizableConcatInput(Node* node) {
//   return state_[node->id()] == State::kInConcat;
// }
bool StringBuilderOptimizer::IsOptimizableConcatInput(Node* node) {
  Status status = GetStatus(node);
  return status.state == State::kInConcat && status.id != -1 &&
         concat_starts_[status.id] != nullptr;
}

// bool StringBuilderOptimizer::CanOptimizeConcat(Node* node) {
//   DCHECK(IsConcat(node));
//   return state_[node->id()] == State::kInConcat ||
//          state_[node->id()] == State::kStartConcat;
// }
bool StringBuilderOptimizer::CanOptimizeConcat(Node* node) {
  DCHECK(IsConcat(node));
  Status status = GetStatus(node);
  return (status.state == State::kInConcat ||
          status.state == State::kBeginConcat) &&
         status.id != -1 && concat_starts_[status.id] != nullptr;
}

// int StringBuilderOptimizer::GetConcatGroup(Node* node) {
//   DCHECK(IsConcat(node));
//   DCHECK(state_[node->id()] == State::kInConcat ||
//          state_[node->id()] == State::kStartConcat);
//   return concats_[node];
// }
int StringBuilderOptimizer::GetConcatGroup(Node* node) {
  DCHECK(IsConcat(node));
  Status status = GetStatus(node);
  DCHECK(status.state == State::kInConcat ||
         status.state == State::kBeginConcat);
  DCHECK_NE(status.id, -1);
  return status.id;
}

// bool StringBuilderOptimizer::IsFirstConcatInGroup(Node* node) {
//   return state_[node->id()] == State::kStartConcat;
// }
bool StringBuilderOptimizer::IsFirstConcatInGroup(Node* node) {
  DCHECK(IsConcat(node));
  Status status = GetStatus(node);
  PrintF("State: ");
  node->Print();
  PrintF("  ==> %s (id=%d)\n", StateToStr(status.state), status.id);
  return status.state == State::kBeginConcat;
}

// Returns a vector containing all of the Phi nodes of the graph.
NodeVector StringBuilderOptimizer::FindPhis() {
  NodeVector phis(temp_zone());
  NodeVector to_visit(temp_zone());
  to_visit.push_back(graph()->end());
  state_[graph()->end()->id()] = State::kSeenInFindPhis;
  while (!to_visit.empty()) {
    Node* node = to_visit.back();
    to_visit.pop_back();
    if (node->opcode() == IrOpcode::kPhi) phis.push_back(node);

    for (Node* input : node->inputs()) {
      if (state_[input->id()] != State::kUnvisited) continue;
      state_[input->id()] = State::kSeenInFindLiterals;
      to_visit.push_back(input);
    }
  }

  return phis;
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
// the replacement only has one use and can safely be marked as State::kInConcat
// and properly optimized in EffectControlLinearizer (in particular, this will
// allow to safely remove StringFromSingleCharCode that are only use for a
// StringConcat that we optimize).
void StringBuilderOptimizer::ReplaceConcatInputIfNeeded(Node* node,
                                                        int input_idx) {
  if (!IsLiteralString(node->InputAt(input_idx), broker())) return;
  if (node->InputAt(input_idx)->UseCount() > 1) {
    Node* new_input = graph()->CloneNode(node->InputAt(input_idx));
    node->ReplaceInput(input_idx, new_input);
    Status node_status = GetStatus(node);
    DCHECK_NE(node_status.id, -1);
    SetStatus(new_input, State::kInConcat, node_status.id);
  }
}

// Returns true if {phi} is a Phi node that looks like it could be part of a
// sequence of StringConcat that we can optimize. That is, if {phi} is for a
// loop (and not an if/else), and has exactly 2 inputs (except the backedge),
// both of which are kStringConcat, and has no uses in the loop besides in a
// kStringLength and kStringConcat.
bool StringBuilderOptimizer::PhiLooksValid(Node* phi) {
  DCHECK_EQ(phi->opcode(), IrOpcode::kPhi);
  if (phi->InputCount() != 3 || !IsConcat(phi->InputAt(0)) ||
      !IsConcat(phi->InputAt(1))) {
    // Wrong number of inputs, or inputs are not kStringConcat.
    PrintF("Wrong phi inputs\n");
    PrintF("phi was loop header = %d\n",
           schedule()->block(phi)->IsLoopHeader());
    return false;
  }
  BasicBlock* phi_block = schedule()->block(phi);
  if (!phi_block->IsLoopHeader()) {
    // {phi} is not for a loop.
    PrintF("Phi is not loop header");
    return false;
  }
  bool has_loop_string_length = false, has_loop_string_concat = false;
  base::SmallVector<Node*, 8> phi_state_values;
  for (Node* node : phi->uses()) {
    BasicBlock* child_block = schedule()->block(node);
    if (!phi_block->LoopContains(child_block)) {
      // {node} is outside the loop, so it doesn't really matter what it
      // contains.
      continue;
    }
    switch (node->opcode()) {
#define CASE(op, var)                                                       \
  case IrOpcode::k##op:                                                     \
    if (var) {                                                              \
      /* Second time we encouter this node type inside the loop, which will \
       * prevent our optimization. */                                       \
      PrintF("Wrong loop opcode thing\n");                                  \
      return false;                                                         \
    }                                                                       \
    var = true;                                                             \
    break;
      CASE(StringConcat, has_loop_string_concat);
      CASE(StringLength, has_loop_string_length);
#undef CASE

      case IrOpcode::kTypedStateValues:
        // The Phi is used in a TypedStateValues node. It's OK, because we'll
        // later check that the StringConcat in this loop are not used anywhere
        // else beside in the next StringConcat, or in a StateValues node that
        // is not the same as {phi}. This ensures that this TypedStateValues
        // node has no dependencies with one of the StringConcat (this would be
        // an issue, since the StringConcat and the Phi are not alive at the
        // same time).
        phi_state_values.push_back(node);
        break;

      default:
        // {phi} has a use inside the loop that is neither a StringLength or a
        // Concat, which means that this loop does not "just build a string",
        // but does something else with the string it's building. In theory, we
        // could check whether both {phi} and the result of the subsequent
        // StringConcat are alive at the same time, and, if not, do the
        // optimization despite the other use. For now, for simplicity, we don't
        // do it, and just don't perform the optimization in this case.
        PrintF("Wrong use node: ");
        node->Print();
        return false;
    }
  }

  if (!has_loop_string_length || !has_loop_string_concat) {
    // The uses of {phi} in the loop are not a StringLength and a
    // StringConcat --> we don't optimize it.
    PrintF("Missing length or concat\n");
    return false;
  }

  // We're now checking that {phi}'s loop contains a chain of StringConcat
  // (from {phi} to {phi}).
  Node* node = phi->InputAt(1);
  while (node != phi) {
    if (!IsConcat(node) || !IsLiteralString(node->InputAt(2), broker())) {
      // One of the nodes in the chain in not a valid StringConcat.
      PrintF("Going back, bad node\n");
      return false;
    }
    if (node->UseCount() == 2) {
      // If {node} has 2 uses, then we allow 2 situations:
      //  - one can be for a StringLength, and the other one a StringConcat.
      //  - one can be {phi} and the other one a StateValues.
      Node *use1 = *node->uses().begin(),
           *use2 = *std::next(node->uses().begin());
      if (!((use1->opcode() == IrOpcode::kStringLength && IsConcat(use2)) ||
            (use2->opcode() == IrOpcode::kStringLength && IsConcat(use1)))) {
        if (use1 == phi || use2 == phi) {
          if (use1->opcode() != IrOpcode::kTypedStateValues &&
              use2->opcode() != IrOpcode::kTypedStateValues) {
            // We are back to {phi}, but the other use is no a TypedStateValues.
            PrintF("Going back because found phi, but bad other use\n");
            return false;
          } else if (std::find(phi_state_values.begin(), phi_state_values.end(),
                               use1) != phi_state_values.end() ||
                     std::find(phi_state_values.begin(), phi_state_values.end(),
                               use2) != phi_state_values.end()) {
            // The TypedStateValues node is in {phi_state_values}, which is
            // going to be an issue, because {phi} and {node} won't be able to
            // be alive at the same time with the String Builder optimization.
            PrintF("Going back because same kTypedStateValues\n");
            return false;
          }
        } else {
          // None of the use is {phi}, and the uses are not StringLength and
          // StringConcat. We don't know how to optimize this.
          PrintF("Going back, bad node use count\n");
          return false;
        }
      }
    }
    if (node->UseCount() > 2) {
      // More than 2 uses, the chain of StringConcat is either not valid, or
      // more complex that we want to handle.
      PrintF("Going back, bad node (> 2 use count)\n");
      PrintF("Uses:\n");
      for (Node* n : node->uses()) {
        n->Print();
      }
      return false;
    }
    node = node->InputAt(1);
  }

  return true;
}

void StringBuilderOptimizer::FindCandidatesFromPhis(NodeVector phis) {
  // TODO(dm): throughout this function, take existing States of Nodes into
  // account to stop early (or to do something else?).

  // TODO(dm): handle if/else.

  PrintF("Got %zu phis to inspect.\n", phis.size());

  // Finding loops that build strings using StringConcat(Phi, literal), where
  // Phi merges two StringConcat.
  NodeVector valid_loop_phis(temp_zone_);
  for (Node* phi : phis) {
    PrintF("\n\nConsidering this phi [1]: ");
    phi->Print();
    PrintF("UseCount=%d\n", phi->UseCount());
    PrintF("Uses:\n");
    for (Node* n : phi->uses()) {
      n->Print(0);
    }
    if (PhiLooksValid(phi)) {
      PrintF("We can optimize this Phi\n");
      valid_loop_phis.push_back(phi);
    } else {
      PrintF("We can't optimize this Phi, because of reasons\n");
    }
    PrintF("\n");
  }

  PrintF("\n\n\nAmong those, %zu are valid\n", valid_loop_phis.size());

  if (valid_loop_phis.empty()) {
    return;
  }

  OneOrTwoByteAnalysis one_or_two_byte_analysis(graph(), temp_zone(), broker());

  // Trying to go back from those loops to the 1st concat.
  for (Node* phi : valid_loop_phis) {
    PrintF("\nConsidering this phi [2]: ");
    phi->Print();
    Node* node = phi->InputAt(0);
    DCHECK(IsConcat(node));
    while (true) {
      PrintF("This node: ");
      node->Print();
      PrintF("Has %d inputs\n", node->InputCount());
      // TODO(dm): account for the possibility that we stumble upon a Phi, which
      // would need to be processed.
      if (!IsConcat(node) || state_[node->id()] == State::kInvalid) {
        state_[node->id()] = State::kInvalid;
        state_[phi->id()] = State::kInvalid;
        PrintF("Not a valid concat :/\n");
        break;
      }
      if (!IsLiteralString(node->InputAt(2), broker())) {
        PrintF("No Literal string input\n");
        // No literal inputs.
        state_[node->id()] = State::kInvalid;
        state_[phi->id()] = State::kInvalid;
        break;
      }

      PrintF("Uses:\n");
      for (Node* n : node->uses()) {
        n->Print();
      }
      // Checking uses
      if (node->UseCount() > 4) {
        // Too many uses
        state_[node->id()] = State::kInvalid;
        state_[phi->id()] = State::kInvalid;
        PrintF("More than 4 uses...\n");
        break;
      }
      bool has_string_length_use = false, has_string_concat_use = false,
           has_typed_state_values_use = false, has_phi_use = false,
           is_valid = true;
      for (Node* n : node->uses()) {
        PrintF("use: ");
        n->Print();
        switch (n->opcode()) {
#define CASE(op, var)   \
  case IrOpcode::k##op: \
    if (var) {          \
      is_valid = false; \
    } else {            \
      var = true;       \
    }                   \
    break;
          CASE(Phi, has_phi_use)
          CASE(StringLength, has_string_length_use)
          CASE(StringConcat, has_string_concat_use)
          CASE(NewConsString, has_string_concat_use)
          CASE(TypedStateValues, has_typed_state_values_use)
#undef CASE

          case IrOpcode::kReturn:
            // TODO(dm): for such returns, we might need to return the
            // left-trimmed SeqString to avoid having SlicedStrings that are
            // less than 13 bytes.
            break;

          default:
            PrintF("Set is_valid to false in default\n");
            is_valid = false;
            break;
        }
      }
      if (has_phi_use && (has_string_length_use || has_string_concat_use)) {
        // One of the uses is a phi node, so there shouldn't be any StringLength
        // or StringConcat in the uses. (note that this condition guarantees
        // that if one of the uses is "a" phi node, then it's actually "the" phi
        // node we are looking at ({phi})).
        is_valid = false;
      }
      if (!is_valid) {
        // The uses are not just one StringLength, one StringConcat and one
        // TypedStateValues or one Phi.
        state_[node->id()] = State::kInvalid;
        state_[phi->id()] = State::kInvalid;
        PrintF("The uses are not good\n");
        break;
      }

      if (IsLiteralString(node->InputAt(1), broker())) {
        DCHECK(IsLiteralString(node->InputAt(2), broker()));
        PrintF("\n\nValid Concat %d Starting at:\n", concat_count_);
        node->Print();
        // Both inputs are literal, we found the starting point.
        state_[node->id()] = State::kStartConcat;
        // Storing all of the nodes of this concat
        PrintF("It actually comes from this phi:\n");
        phi->Print();
        PrintF("Through those nodes:\n");
        concats_[node] = concat_count_;
        concat_starts_.push_back(node);
        OneOrTwoByteAnalysis::State one_or_two_byte =
            one_or_two_byte_analysis.OneOrTwoByte(node);
        // Duplicating the inputs, to make sure that this StringConcat is the
        // only user.
        ReplaceConcatInputIfNeeded(node, 1);
        ReplaceConcatInputIfNeeded(node, 2);
        for (Node* n = phi->InputAt(0); n != node; n = n->InputAt(1)) {
          n->Print();
          concats_[n] = concat_count_;
          state_[n->id()] = State::kInConcat;
          if (IsConcat(n)) {
            one_or_two_byte = OneOrTwoByteAnalysis::MergeStates(
                one_or_two_byte, one_or_two_byte_analysis.OneOrTwoByte(n));
            ReplaceConcatInputIfNeeded(n, 2);
          }
        }
        state_[phi->id()] = State::kConcatEnd;
        concats_[phi] = concat_count_;
        PrintF("And the loop contains:\n");
        for (Node* n = phi->InputAt(1); n != phi; n = n->InputAt(1)) {
          n->Print();
          concats_[n] = concat_count_;
          state_[n->id()] = State::kInConcat;
          if (IsConcat(n)) {
            one_or_two_byte = OneOrTwoByteAnalysis::MergeStates(
                one_or_two_byte, one_or_two_byte_analysis.OneOrTwoByte(n));
            one_or_two_byte_analysis.OneOrTwoByte(n);
            ReplaceConcatInputIfNeeded(n, 2);
          }
        }
        // Collecting blocks outside the loop that are successors from the loop:
        // those are the blocks where we'll need to perform the right-trimming
        // of the backing-store.
        PrintF("Searching for exit blocks\n");
        BasicBlock* loop_header = schedule()->block(phi);
        ZoneVector<BasicBlock*> loop_successors(temp_zone());
        for (BasicBlock* block = loop_header;
             block->rpo_number() < loop_header->loop_end()->rpo_number();
             block = block->rpo_next()) {
          PrintF("Checking exits of block %d\n", block->id().ToInt());
          for (BasicBlock* next : block->successors()) {
            PrintF("  What about block %d?\n", next->id().ToInt());
            if (!loop_header->LoopContains(next) &&
                std::find(loop_successors.begin(), loop_successors.end(),
                          next) == loop_successors.end()) {
              loop_successors.push_back(next);
              PrintF("  Yes!!!\n");
            } else {
              PrintF("  Nah...\n");
            }
          }
        }
        trimming_blocks.push_back(loop_successors);
        concat_lasts.push_back(phi);
        concat_one_or_two_bytes_.push_back(one_or_two_byte);
        PrintF("Done with this concat/phi for now\n");
        concat_count_++;
        break;
      }
      node = node->InputAt(1);
    }
  }

  PrintF("\n\nconcat_count = %d\n", concat_count_);

  if (concat_count_ == 0) return;

  // TODO(dm): fix that
  // Trying to add nodes following valid concat to the concat
  for (Node* phi : valid_loop_phis) {
    PrintF("\nConsidering this phi: ");
    phi->Print();
    if (state_[phi->id()] != State::kInConcat) {
      PrintF("Not in concat, moving on\n");
      continue;
    } else {
      PrintF("In concat, let's go!!\n");
    }
    int this_concat_number = concats_[phi];
    PrintF("Trying to extend concat %d\n", this_concat_number);
    Node* node = phi;
    while (true) {
      Node* next_concat = nullptr;
      for (Node* n : node->uses()) {
        if (IsConcat(n) &&
            // Sounds like this should be .find(n), no?
            concats_.find(node) == concats_.end()) {
          if (next_concat == nullptr) {
            next_concat = n;
          } else {
            // multiple concats following the phi. Setting {next_concat} to
            // nuulptr and breaking.
            next_concat = nullptr;
            break;
          }
        }
      }
      if (next_concat == nullptr) break;
      if (!IsLiteralString(next_concat->InputAt(2), broker())) break;
      concats_[next_concat] = this_concat_number;
      node = next_concat;
    }
  }

  PrintF("The end\n\n\n");
}

void StringBuilderOptimizer::ComputeTrimmingsPerBlocks() {
  PrintF("ComputeTrimmingsPerBlocks\n");
  for (unsigned int i = 0; i < concat_count_; i++) {
    PrintF("On concat %d\n", i);
    ZoneVector<BasicBlock*> concat_nexts = trimming_blocks[i];
    Node* phi = concat_lasts[i];
    for (BasicBlock* block : concat_nexts) {
      PrintF("Found one next, ID = %d\n", block->id().ToInt());
      if (!reverse_trimmings[block->id().ToInt()].has_value()) {
        reverse_trimmings[block->id().ToInt()] = ZoneVector<Node*>(temp_zone());
      }
      reverse_trimmings[block->id().ToInt()]->push_back(phi);
    }
  }
}

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

// Compute up to {kMaxPredecessors} predecessors of {start} that are not past
// {end}, and store them in {dst}.
bool ComputePredecessors(BasicBlock* start, BasicBlock* end,
                         ZoneVector<BasicBlock*> dst) {
  static constexpr int kMaxPredecessors = 10;
  dst.clear();
  dst.push_back(start);
  unsigned long stack_pointer = 0;
  while (stack_pointer < dst.size() && dst.size() <= kMaxPredecessors) {
    BasicBlock* current = dst[stack_pointer++];
    if (current == end) continue;
    for (BasicBlock* pred : current->predecessors()) {
      if (std::find(dst.begin(), dst.end(), pred) == dst.end()) {
        dst.push_back(pred);
      }
    }
  }
  return dst.size() <= kMaxPredecessors;
}

// Return true of {maybe_dominator} dominates {maybe_dominee} and is less than
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
        return false;
      }
      if (!ComesBeforeInBlock(other_child, concat_child, child_block)) {
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

    // TODO(dm): the following check looks like a special case of a more generic
    // check that we could do instead.
    if (other_child->opcode() == IrOpcode::kPhi &&
        !other_child_block->IsLoopHeader() && other_child->InputCount() == 3) {
      if ((other_child->InputAt(0) == concat_child &&
           other_child->InputAt(1) == node) ||
          (other_child->InputAt(1) == concat_child &&
           other_child->InputAt(0) == node)) {
        // {other_child} is a Phi that merges {child} and {node}.
        continue;
      }
    }

    bool all_other_predecessors_computed =
        ComputePredecessors(other_child_block, node_block, other_predecessors_);

    // Making sure that {child_block} isn't in the predecessors of
    // {other_child_block}. Otherwise, the use of {node} in {other_child}
    // would be invalid.
    if (std::find(other_predecessors_.begin(), other_predecessors_.end(),
                  child_block) != other_predecessors_.end()) {
      // {child} is in the predecessor of {other_child}, which is definitely
      // invalid (because it means that {other_child} uses an out-dated version
      // of {node}, since {child} modified it).
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
          ComputePredecessors(child_block, node_block, current_predecessors_);
        }
        if (std::find(current_predecessors_.begin(),
                      current_predecessors_.end(),
                      other_child_block) == current_predecessors_.end()) {
          // We didn't find {other_child} in the predecessors of {child}. It
          // means that either {other_child} comes after in the graph (which
          // is unsafe), or that {other_child} and {child} are on two
          // independent subgraphs (which is safe). We have no efficient way
          // to know which one of the two this is, so, we fall back to a
          // stricter approach: the use of {node} in {other_child} is
          // guaranteed to be safe if {other_child_block} dominates
          // {child_block}.
          if (!IsDominator(other_child_block, child_block)) {
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
      // TODO(dmercadier): Consider handling arbitrary strings on the rhs.
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
      if (HasConcatOrPhiUse(lhs)) {
        PrintF("--> kBeginConcat!\n");
        SetStatus(node, State::kBeginConcat, concat_count_++);
        concat_starts_.push_back(node);
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
    // TODO(dm): if one of the inputs of the kPhi is a string constant, then it
    // should count as a kInConcat.
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
            SetConcatHasLoopPhi(status.id);
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

// For each potential concatenation, checks that it is correct: all of their
// nodes should have status kInConcat (except the 1st one that is kBeginConcat),
// they should contain at least one loop phi, and they should not contain any
// kPendingPhi.
// We also collect all the possible ends for each concat (their can be multiple
// possible ends if there is a branch before the end of a concat), and make sure
// that they don't conflict with each others (remember that we check node uses
// with CheckPreviousNodeUses, only after having computed the next node of the
// concat, except that the ends of the concat have no "next node", and we thus
// never checked that their uses were correct). If multiple ends conflict with
// each others, we end it at the last loop phi that is on the "main path",
// assuming that it wasn't already one of the possible ends ("main path" = not
// in a path that is not always taken by the concatenation, ie, not in a if/else
// where the concat continues in both branches).
// When a concat is invalid (invalid end or invalid nodes inside), we don't set
// the status of its nodes to kInvalid, but rather set the corresponding entry
// of {concat_starts_} to nullptr: this avoid having to iterate over the concat
// once again. Then, to check if a node is a valid node inside a concat, we have
// to do: "node.status.state == kInConcat && concats_starts[node.status.id] !=
// nullptr".
void StringBuilderOptimizer::FinishConcatenations() {
  OneOrTwoByteAnalysis one_or_two_byte_analysis(graph(), temp_zone(), broker());

  for (unsigned int concat_id = 0; concat_id < concat_count_; concat_id++) {
    PrintF("\nFinishing concat %d\n", concat_id);
    Node* start = concat_starts_[concat_id];
    OneOrTwoByteAnalysis::State one_or_two_byte =
        one_or_two_byte_analysis.OneOrTwoByte(start);
    Status start_status = GetStatus(start);
    if (start_status.state != State::kBeginConcat ||
        !GetConcatHasLoopPhi(concat_id)) {
      // {start} has already been invalidated, or the concatenation doesn't
      // contain a loop Phi. We set the corresponding entry in {concat_starts_}
      // to nullptr, in order to be able to use this later to figure out if a
      // node is really in a concat or not.
      PrintF("Start is wrong?!\n");
      concat_starts_[concat_id] = nullptr;
      continue;
    }
    DCHECK_EQ(start_status.state, State::kBeginConcat);
    DCHECK_EQ(start_status.id, concat_id);

    // TODO(dm): reuse {queue}, {seen} and {ends} from one iteration to the next
    // one.
    ZoneVector<Node*> queue(temp_zone());
    ZoneVector<Node*> seen(temp_zone());  // TODO(dm): use a hashmap instead?
    ZoneVector<Node*> ends(temp_zone());
    queue.push_back(start);
    while (!queue.empty()) {
      Node* curr = queue.back();
      queue.pop_back();
      if (std::find(seen.begin(), seen.end(), curr) != seen.end()) continue;
      seen.push_back(curr);

      PrintF("Node in concat: ");
      curr->Print();

      Status curr_status = GetStatus(curr);
      PrintF(" $ state = %s\n", StateToStr(curr_status.state));
      DCHECK(curr_status.state == State::kInConcat ||
             curr_status.state == State::kBeginConcat);
      DCHECK_IMPLIES(curr_status.state == State::kBeginConcat, curr == start);
      DCHECK_EQ(curr_status.id, start_status.id);

      if (IsConcat(curr)) {
        one_or_two_byte = OneOrTwoByteAnalysis::MergeStates(
            one_or_two_byte, one_or_two_byte_analysis.OneOrTwoByte(curr));
        // Duplicating string inputs if needed, and marking them as InConcat (so
        // that EffectControlLinearizer doesn't lower them).
        // TODO(dm): should we get rid of the 1st input of such concat (the
        // length), since we don't use it?
        ReplaceConcatInputIfNeeded(curr, 1);
        ReplaceConcatInputIfNeeded(curr, 2);
      }

      bool has_next = false;
      for (Node* next : curr->uses()) {
        Status next_status = GetStatus(next);
        if (next_status.state == State::kInConcat &&
            next_status.id == curr_status.id) {
          queue.push_back(next);
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

    // If the concatenation has multiple possible ends, we need to make sure
    // that all of the uses of all the ends have no conflicts: none of the uses
    // of end A should have end B in its predecessors for all ends A and B.
    // Actually, I think that this is already guaranteed by construciton.
    // TODO(dm): check if this is guaranteed or not...

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
          if (!reverse_trimmings[block->id().ToInt()].has_value()) {
            reverse_trimmings[block->id().ToInt()] =
                ZoneVector<Node*>(temp_zone());
          }
          reverse_trimmings[block->id().ToInt()]->push_back(end);
        }
        UpdateStatus(end, State::kEndConcatLoopPhi);
      } else {
        UpdateStatus(end, State::kEndConcat);
      }
    }

    concat_one_or_two_bytes_.push_back(one_or_two_byte);
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

void StringBuilderOptimizer::Analyze() {
  PrintF("\n\n@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ ANALYZE @@@@@@@@@@@@@@@@@@\n\n\n");
  // NodeVector phis = FindPhis();
  // FindCandidatesFromPhis(phis);
  // ComputeTrimmingsPerBlocks();
  VisitGraph();
  PrintF("\n\nDone with StringBuilderOptimizer\n\n\n");
}

void StringBuilderOptimizer::Run() { Analyze(); }

StringBuilderOptimizer::StringBuilderOptimizer(JSGraph* jsgraph,
                                               Schedule* schedule,
                                               Zone* temp_zone,
                                               JSHeapBroker* broker)
    : jsgraph_(jsgraph),
      schedule_(schedule),
      temp_zone_(temp_zone),
      broker_(broker),
      state_(jsgraph->graph()->NodeCount(), State::kUnvisited, temp_zone),
      concat_starts_(temp_zone),
      trimming_blocks(temp_zone),
      reverse_trimmings(schedule->BasicBlockCount(), temp_zone),
      concat_lasts(temp_zone),
      concat_one_or_two_bytes_(temp_zone),
      status_(jsgraph->graph()->NodeCount(), Status{-1, State::kUnvisited},
              temp_zone),
      current_predecessors_(temp_zone),
      other_predecessors_(temp_zone),
      concat_has_loop_phi_(temp_zone) {}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
