// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/string-builder-optimizer.h"

#include "src/base/logging.h"
#include "src/base/small-vector.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/graph-assembler.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-heap-broker.h"
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

// TODO(dm): handle NewConsString in addition to StringConcat

bool StringBuilderOptimizer::IsLiteralString(Node* node) {
  if (node->opcode() == IrOpcode::kHeapConstant) {
    HeapObjectMatcher m(node);
    return m.HasResolvedValue() && m.Ref(broker()).IsString();
  }
  if (node->opcode() == IrOpcode::kStringFromSingleCharCode) {
    return true;
  }
  return false;
}

bool StringBuilderOptimizer::IsOptimizableConcatInput(Node* node) {
  return state_[node->id()] == State::kInConcat;
}

bool StringBuilderOptimizer::CanOptimizeConcat(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kStringConcat);
  return state_[node->id()] == State::kInConcat ||
         state_[node->id()] == State::kStartConcat;
}

int StringBuilderOptimizer::GetConcatGroup(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kStringConcat);
  DCHECK(state_[node->id()] == State::kInConcat ||
         state_[node->id()] == State::kStartConcat);
  return concats_[node];
}

bool StringBuilderOptimizer::IsFirstConcatInGroup(Node* node) {
  return state_[node->id()] == State::kStartConcat;
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

// Duplicates the {input_idx}th input of {node} if it has multiple uses, so that
// the replacement only has one use and can safely be marked as State::kInConcat
// and properly optimized in EffectControlLinearizer (in particular, this will
// allow to safely remove StringFromSingleCharCode that are only use for a
// StringConcat that we optimize).
void StringBuilderOptimizer::ReplaceConcatInputIfNeeded(Node* node,
                                                        int input_idx) {
  if (node->InputAt(input_idx)->UseCount() > 1) {
    Node* new_input = graph()->CloneNode(node->InputAt(input_idx));
    node->ReplaceInput(input_idx, new_input);
    if (new_input->id() >= state_.size()) {
      state_.resize(new_input->id() + 1);
    }
    state_[new_input->id()] = State::kInConcat;
  }
}

// Returns true if {phi} is a Phi node that looks like it could be part of a
// sequence of StringConcat that we can optimize. That is, if {phi} is for a
// loop (and not an if/else), and has exactly 2 inputs (except the backedge),
// both of which are kStringConcat, and has no uses in the loop besides in a
// kStringLength and kStringConcat.
bool StringBuilderOptimizer::PhiLooksValid(Node* phi) {
  DCHECK_EQ(phi->opcode(), IrOpcode::kPhi);
  if (phi->InputCount() != 3 ||
      phi->InputAt(0)->opcode() != IrOpcode::kStringConcat ||
      phi->InputAt(1)->opcode() != IrOpcode::kStringConcat) {
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
    if (node->opcode() != IrOpcode::kStringConcat ||
        !IsLiteralString(node->InputAt(2))) {
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
      if (!((use1->opcode() == IrOpcode::kStringLength &&
             use2->opcode() == IrOpcode::kStringConcat) ||
            (use2->opcode() == IrOpcode::kStringLength &&
             use1->opcode() == IrOpcode::kStringConcat))) {
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

  // Trying to go back from those loops to the 1st concat.
  for (Node* phi : valid_loop_phis) {
    PrintF("\nConsidering this phi [2]: ");
    phi->Print();
    Node* node = phi->InputAt(0);
    DCHECK_EQ(node->opcode(), IrOpcode::kStringConcat);
    while (true) {
      PrintF("This node: ");
      node->Print();
      PrintF("Has %d inputs\n", node->InputCount());
      // TODO(dm): account for the possibility that we stumble upon a Phi, which
      // would need to be processed.
      if (node->opcode() != IrOpcode::kStringConcat ||
          state_[node->id()] == State::kInvalid) {
        state_[node->id()] = State::kInvalid;
        state_[phi->id()] = State::kInvalid;
        break;
      }
      if (!IsLiteralString(node->InputAt(2))) {
        PrintF("No Literal string input\n");
        // No literal inputs.
        state_[node->id()] = State::kInvalid;
        state_[phi->id()] = State::kInvalid;
        break;
      }
      // Checking uses
      if (node->UseCount() > 3) {
        // Too many uses
        state_[node->id()] = State::kInvalid;
        state_[phi->id()] = State::kInvalid;
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

      if (IsLiteralString(node->InputAt(1))) {
        DCHECK(IsLiteralString(node->InputAt(2)));
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
        // Duplicating the inputs, to make sure that this StringConcat is the
        // only user.
        ReplaceConcatInputIfNeeded(node, 1);
        ReplaceConcatInputIfNeeded(node, 2);
        for (Node* n = phi->InputAt(0); n != node; n = n->InputAt(1)) {
          n->Print();
          concats_[n] = concat_count_;
          state_[n->id()] = State::kInConcat;
          if (n->opcode() == IrOpcode::kStringConcat) {
            ReplaceConcatInputIfNeeded(n, 2);
          }
        }
        state_[phi->id()] = State::kInConcat;
        concats_[phi] = concat_count_;
        PrintF("And the loop contains:\n");
        for (Node* n = phi->InputAt(1); n != phi; n = n->InputAt(1)) {
          n->Print();
          concats_[n] = concat_count_;
          state_[n->id()] = State::kInConcat;
          if (n->opcode() == IrOpcode::kStringConcat) {
            ReplaceConcatInputIfNeeded(n, 2);
          }
        }
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
        if (n->opcode() == IrOpcode::kStringConcat &&
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
      if (!IsLiteralString(next_concat->InputAt(2))) break;
      concats_[next_concat] = this_concat_number;
      node = next_concat;
    }
  }

  PrintF("The end\n\n\n");
}

void StringBuilderOptimizer::Analyze() {
  PrintF("\n\n@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ ANALYZE @@@@@@@@@@@@@@@@@@\n\n\n");
  NodeVector phis = FindPhis();
  FindCandidatesFromPhis(phis);

  PrintF("\n\nCandidates: \n");
  for (Node* node : candidates_) {
    PrintF("  ");
    node->Print(0);
    PrintF(" == %s\n", StateToStr(state_[node->id()]));
  }
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
      candidates_(temp_zone) {}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
