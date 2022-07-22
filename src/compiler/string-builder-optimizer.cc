// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/string-builder-optimizer.h"

#include "src/base/logging.h"
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

// namespace {
// int RealUseCount(Node* node) {
//   int use_count = 0;
//   for (Node* next : node->uses()) {
//     switch (next->opcode()) {
//       case IrOpcode::kTypedStateValues:
//       case IrOpcode::kStateValues:
//         break;
//       default:
//         use_count++;
//     }
//   }
//   return use_count;
// }
// }

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

// void StringBuilderOptimizer::Invalidate(Node* node) {
//   state_.Set(node, State::kInvalid);
//   for (int i = 0; i < node->op()->ValueInputCount(); i++) {
//     Invalidate(node->InputAt(i));
//   }
// }

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

// StringBuilderOptimizer::State StringBuilderOptimizer::DiscoverNode(Node*
// node) {
//   if (state_.Get(node) != State::kUnvisited) return state_.Get(node);
//   if (IsLiteralString(node)) {
//     state_.Set(node, State::kLiteralString);
//     return State::kLiteralString;
//   }
//   state_.Set(node, State::kPending);

//   switch (node->opcode()) {
//     case IrOpcode::kStringConcat: {
//       // Note that input 0 is the length of th StringConcat
//       Node* lhs = node->InputAt(1);
//       Node* rhs = node->InputAt(2);
//       State this_state = State::kInvalid;
//       if (IsLiteralString(lhs) || IsLiteralString(rhs)) {
//         if (IsLiteralString(lhs) && IsLiteralString(rhs)) {
//           candidates_.push_back(node);
//           this_state = State::kValidConcat;
//         } else {
//           Node* other = IsLiteralString(lhs) ? rhs : lhs;
//           State other_state = DiscoverNode(other);
//           if (other_state == State::kValidConcat ||
//               other_state == State::kPending) {
//             candidates_.push_back(node);
//             this_state = RealUseCount(node) == 1 ? State::kValidConcat
//                                                : State::kFinalConcat;
//           } else {
//             this_state = State::kInvalid;
//           }
//         }
//       }
//       state_.Set(node, this_state);
//       return this_state;
//     }
//     case IrOpcode::kPhi: {
//       for (int i = 0; i < node->op()->ValueInputCount(); i++) {
//         State in_state = DiscoverNode(node->InputAt(i));
//         if (in_state != State::kLiteralString &&
//             in_state != State::kValidConcat && in_state != State::kPending) {
//           state_.Set(node, State::kInvalid);
//           return State::kInvalid;
//         }
//       }
//       state_.Set(node, State::kValidConcat);
//       return State::kValidConcat;
//     }
//     // case IrOpcode::kStringConcat: {
//     //   Node* lhs = node->InputAt(1);
//     //   Node* rhs = node->InputAt(2);
//     //   if ((DiscoverNode(lhs) == State::kLiteralString &&
//     //        DiscoverNode(rhs) == State::kValidConcat) ||
//     //       (DiscoverNode(lhs) == State::kLiteralString &&
//     //        DiscoverNode(rhs) == State::kValidConcat)) {
//     //     state_.Set(node, State::kValidConcat);
//     //     return State::kValidConcat;
//     //   } else {
//     //     Invalidate(lhs);
//     //     Invalidate(rhs);
//     //     return State::kInvalid;
//     //   }
//     // }
//     // case IrOpcode::kPhi: {
//     //   if (node->op()->ValueInputCount() != 2) {
//     //      state_.Set(node, State::kInvalid);
//     //     return State::kInvalid;
//     //   }
//     //   Node* lhs = node->InputAt(0);
//     //   Node* rhs = node->InputAt(1);
//     //   if ((DiscoverNode(lhs) == State::kLiteralString &&
//     //        DiscoverNode(rhs) == State::kValidConcat) ||
//     //       (DiscoverNode(lhs) == State::kLiteralString &&
//     //        DiscoverNode(rhs) == State::kValidConcat)) {
//     //     state_.Set(node, State::kValidConcat);
//     //     return State::kValidConcat;
//     //   } else {
//     //     return State::kInvalid;
//     //   }
//     // }
//     default:
//       state_.Set(node, State::kInvalid);
//       return State::kInvalid;
//   }
//   UNREACHABLE();
// }

// NodeVector StringBuilderOptimizer::FindStringLiterals() {
//   NodeVector string_literals(temp_zone());
//   NodeVector to_visit(temp_zone());
//   to_visit.push_back(graph_->end());
//   state_[graph_->end()->id()] = State::kSeenInFindLiterals;
//   while (!to_visit.empty()) {
//     Node* node = to_visit.back();
//     to_visit.pop_back();
//     if (IsLiteralString(node)) string_literals.push_back(node);

//     for (Node* input : node->inputs()) {
//       if (state_[input->id()] != State::kUnvisited) continue;
//       state_[input->id()] = State::kSeenInFindLiterals;
//       to_visit.push_back(input);
//     }
//   }

//   return string_literals;
// }

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
//
// void StringBuilderOptimizer::FindCandidatesFromStringLiterals(NodeVector
// string_literals) {
//   for (Node* string_node : string_literals) {
//     for (Node* user : string_node->uses()) {
//       if (user->opcode() == IrOpcode::kStringConcat) {
//         TryCandidate(user);
//         //state_.Set(user, State::kStart);
//       }
//     }
//   }
// }

// void StringBuilderOptimizer::TryCandidate(Node* start) {
//   // TODO(dm): handle nested loops.
//   DCHECK_EQ(start->opcode(), IrOpcode::kStringConcat);
//   if (!IsLiteralString(start->InputAt(0)) ||
//       !IsLiteralString(start->InputAt(1))) {
//     // The initial StringConcat isn't between 2 literal strings.
//     return;
//   }
//   // Speculatively setting Start status.
//   state_.Set(start, State::kStartConcat);

//   Node* node = start;
//   while (node->UseCount() == 1) {
//     node = *node->uses().begin();
//     if (node->opcode() != IrOpcode::kStringConcat) {
//       break;
//     }
//     if (!IsLiteralString(node->InputAt(0)) &&
//         !IsLiteralString(node->InputAt(1))) {
//       // No literal input.
//       // TODO(dm): restore state
//       return;
//     }
//     state_.Set(node, State::kValidConcat);
//   }

//   if (node->opcode() != IrOpcode::kPhi || node->UseCount() != 2) {
//     // TODO(dm): restore states
//     return;
//   }

//   state_.Set(node, State::kPending);
//   node = *node->uses().begin();
//   while (node->UseCount() == 1) {
//     if (node == start) {
//       // The start was inside the loop. We can't optimize from there.
//       // // TODO(dm): restore states
//       return;
//     }
//     if (node->opcode() != IrOpcode::kStringConcat) {
//       // TODO(dm): this could be a Phi if we have nested loops.
//       // TODO(dm): restore states
//       return;
//     }
//     state_.Set(node, State::kValidConcat);
//   }

//   if (node->opcode() != IrOpcode::kPhi) {
//     // We didn't end up on the original Phi.
//     // TODO(dm): restore states
//     return;
//   }
// }

void StringBuilderOptimizer::FindCandidatesFromPhis(NodeVector phis) {
  // TODO(dm): throughout this function, take existing States of Nodes into
  // account to stop early (or to do something else?).

  PrintF("Got %zu phis to inspect.\n", phis.size());

  // Finding loops that build strings using StringConcat(Phi, literal), where
  // Phi merges two StringConcat.
  NodeVector valid_loop_phis(temp_zone_);
  for (Node* phi : phis) {
    PrintF("Considering this phi: ");
    phi->Print();
    PrintF("UseCount=%d\n", phi->UseCount());
    PrintF("Uses:\n");
    for (Node* n : phi->uses()) {
      n->Print(0);
    }
    PrintF("\n");
    if (phi->UseCount() != 4 || phi->InputCount() != 3 ||
        phi->InputAt(0)->opcode() != IrOpcode::kStringConcat ||
        phi->InputAt(1)->opcode() != IrOpcode::kStringConcat) {
      PrintF(
          "Stoping early (bad usecount, or bad input count, or bad inputs)\n");
      state_[phi->id()] = State::kInvalid;
      continue;
    }
    // Extracting the Phi's uses
    Node *string_concat = nullptr, *string_length = nullptr,
         *state_values = nullptr, *other = nullptr;
    bool failed = false;
    for (Node* node : phi->uses()) {
      switch (node->opcode()) {
#define CASE(op, var)         \
  case IrOpcode::k##op:       \
    if (var != nullptr) {     \
      if (other != nullptr) { \
        failed = true;        \
      } else {                \
        other = node;         \
      }                       \
    } else {                  \
      var = node;             \
    }                         \
    break;
        CASE(StringConcat, string_concat);
        CASE(StringLength, string_length);
        CASE(TypedStateValues, state_values);
#undef CASE
        default:
          if (other != nullptr) {
            failed = true;
          } else {
            other = node;
          }
          break;
      }
    }
    if (failed == true || string_concat == nullptr ||
        string_length == nullptr) {
      PrintF("failed or missing string_concat or missing string_length\n");
      state_[phi->id()] = State::kInvalid;
      continue;
    }
    Node* node = phi->InputAt(1);
    while (true) {
      PrintF("This node: ");
      // node->Print();
      PrintF("Has %d inputs\n", node->InputCount());
      if (node == phi) {
        PrintF("We're back to the phi\n");
        state_[phi->id()] = State::kValidConcatPhi;
        break;
      }
      if (node->opcode() != IrOpcode::kStringConcat ||
          !IsLiteralString(node->InputAt(2))) {
        if (node->opcode() != IrOpcode::kStringConcat) {
          PrintF("Wrong opcode\n");
        }
        if (!IsLiteralString(node->InputAt(2))) {
          PrintF("2nd input is not literal string:\n");
          node->InputAt(2)->Print();
        }
        PrintF("Wrong opcode or no litteral string in inputs\n");
        // Marking previous nodes of the loop as invalid
        for (Node* n = node; n != phi; n = n->InputAt(1)) {
          state_[n->id()] = State::kInvalid;
        }
        break;
      }
      node = node->InputAt(1);
    }

    if (state_[phi->id()] == State::kValidConcatPhi) {
      valid_loop_phis.push_back(phi);
    }
  }

  PrintF("\n\n\nAmong those, %zu are valid\n", valid_loop_phis.size());

  // Trying to go back from those loops to the 1st concat.
  for (Node* phi : valid_loop_phis) {
    PrintF("\nConsidering this phi: ");
    phi->Print();
    Node* node = phi->InputAt(0);
    DCHECK_EQ(node->opcode(), IrOpcode::kStringConcat);
    while (true) {
      PrintF("This node: ");
      // node->Print();
      PrintF("Has %d inputs\n", node->InputCount());
      // (dm): account for the possibility that we stumble upon a Phi, which
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
        Node* new_lhs = graph()->CloneNode(node->InputAt(1));
        Node* new_rhs = graph()->CloneNode(node->InputAt(2));
        node->ReplaceInput(1, new_lhs);
        node->ReplaceInput(2, new_rhs);
        state_[new_lhs->id()] = State::kInConcat;
        state_[new_rhs->id()] = State::kInConcat;
        for (Node* n = phi->InputAt(0); n != node; n = n->InputAt(1)) {
          n->Print();
          concats_[n] = concat_count_;
          state_[n->id()] = State::kInConcat;
          if (n->opcode() == IrOpcode::kStringConcat) {
            Node* new_rhs = graph()->CloneNode(n->InputAt(2));
            n->ReplaceInput(2, new_rhs);
            state_[new_rhs->id()] = State::kInConcat;
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
            Node* new_rhs = graph()->CloneNode(n->InputAt(2));
            n->ReplaceInput(2, new_rhs);
            state_[new_rhs->id()] = State::kInConcat;
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

  // (dm): fix that
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
  // Finding String literals (HeapConstant or StringFromCharCode)
  // NodeVector string_literals = FindStringLiterals();

  // Iterating down the graph from the lietral strings
  // FindCandidatesFromStringLiterals(string_literals);

  NodeVector phis = FindPhis();
  FindCandidatesFromPhis(phis);

  // for (BasicBlock* block : *(schedule()->rpo_order())) {
  //   for (Node* node : *block) {
  //     DiscoverNode(node);
  //   }
  // }

  PrintF("\n\nCandidates: \n");
  for (Node* node : candidates_) {
    PrintF("  ");
    node->Print(0);
    PrintF(" == %s\n", StateToStr(state_[node->id()]));
  }

  // PrintF("\n\n@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
  //   for (BasicBlock* block : *(schedule()->rpo_order())) {
  //     for (Node* node : *block) {
  //       node->Print(0);
  //       PrintF("  --> %s\n", StateToStr(state_.Get(node)));
  //     }
  //   }
  // PrintF("\n@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n\n\n");
}

void StringBuilderOptimizer::Run() {
  Analyze();
  // AddStringBuilders();
}

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
