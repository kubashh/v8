// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/new-escape-analysis.h"

#include "src/bootstrapper.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

#ifdef DEBUG
thread_local int TraceScope::depth = 0;
#endif

void EffectGraphReducer::ReduceFrom(Node* node) {
  DCHECK(stack_.empty());
  stack_.push({node, 0});
  while (!stack_.empty()) {
    auto& top = stack_.top();
    if (top.input_index < top.node->InputCount()) {
      Node* input = top.node->InputAt(top.input_index);
      top.input_index++;
      switch (state_.Get(input)) {
        case State::kVisited:
        case State::kOnStack:
          break;
        case State::kUnvisited:
        case State::kRevisit: {
          state_.Set(input, State::kOnStack);
          stack_.push({input, 0});
        } break;
      }
    } else {
      state_.Set(top.node, State::kVisited);
      Reduction reduction = Reduce(node);
      for (Edge edge : node->use_edges()) {
        Node* use = edge.from();
        if (NodeProperties::IsEffectEdge(edge)) {
          if (reduction.effect_changed()) Revisit(use);
        } else {
          if (reduction.value_changed()) Revisit(use);
        }
      }
      stack_.pop();
      while (stack_.empty() && !revisit_.empty()) {
        Node* revisit = revisit_.top();
        if (state_.Get(revisit) == State::kRevisit) {
          state_.Set(revisit, State::kOnStack);
          stack_.push({revisit, 0});
        }
        revisit_.pop();
      }
    }
  }
}

void EffectGraphReducer::Revisit(Node* node) {
  if (state_.Get(node) == State::kVisited) {
    TRACE("  Queueing for revisit: %s#%d\n", node->op()->mnemonic(),
          node->id());
    state_.Set(node, State::kRevisit);
    revisit_.push(node);
  } else {
    const char* state;
    switch (state_.Get(node)) {
      case State::kUnvisited:
        state = "kUnivisited";
        break;
      case State::kRevisit:
        state = "kRevisit";
        break;
      case State::kOnStack:
        state = "kOnStack";
        break;
      case State::kVisited:
        state = "kVisited";
        break;
    }
    TRACE("  Use %s#%d is currently %s\n", node->op()->mnemonic(), node->id(),
          state);
  }
}

void EffectGraphReducer::ReduceGraph() {
  roots_.push(graph_->end());
  while (!roots_.empty()) {
    Node* root = roots_.top();
    roots_.pop();
    ReduceFrom(root);
  }
}

VariableStates::VariableStates(JSGraph* graph, EffectGraphReducer* reducer,
                               Zone* zone)
    : zone_(zone),
      graph_(graph),
      table_(zone, State(zone)),
      buffer_(zone),
      reducer_(reducer) {}

VariableStates::Scope::Scope(VariableStates* states, Node* node,
                             Reduction* reduction)
    : ReduceScope(node, reduction),
      states_(states),
      current_state_(states->zone_) {
  switch (node->opcode()) {
    case IrOpcode::kEffectPhi:
      current_state_ = states_->MergeInputs(node);
      break;
    default:
      int effect_inputs = node->op()->EffectInputCount();
      if (effect_inputs == 1) {
        current_state_ =
            states_->table_[NodeProperties::GetEffectInput(node, 0)];
      } else {
        DCHECK_EQ(0, effect_inputs);
      }
  }
}

VariableStates::Scope::~Scope() {
  if (!reduction()->effect_changed() &&
      states_->table_[current_node()] != current_state_) {
    reduction()->set_effect_changed();
  }
  states_->table_[current_node()] = current_state_;
}

VariableStates::State VariableStates::MergeInputs(Node* effect_phi) {
  DCHECK(effect_phi->opcode() == IrOpcode::kEffectPhi);
  int arity = effect_phi->op()->EffectInputCount();
  Node* control = NodeProperties::GetControlInput(effect_phi, 0);
  TRACE("control: %s#%d\n", control->op()->mnemonic(), control->id());
  bool is_loop = control->opcode() == IrOpcode::kLoop;
  buffer_.reserve(arity + 1);

  State result = table_[NodeProperties::GetEffectInput(effect_phi, 0)];
  for (auto pair : result) {
    if (Node* value = pair.second) {
      Variable var = pair.first;
      TRACE("var %i:\n", var.id_);
      buffer_.clear();
      buffer_.push_back(value);
      bool identical_inputs = true;
      int num_defined_inputs = 1;
      TRACE("  input 0: %s#%d\n", value->op()->mnemonic(), value->id());
      for (int i = 1; i < arity; ++i) {
        Node* next_value =
            table_[NodeProperties::GetEffectInput(effect_phi, i)][var];
        if (next_value != value) identical_inputs = false;
        if (next_value != nullptr) {
          num_defined_inputs++;
          TRACE("  input %i: %s#%d\n", i, next_value->op()->mnemonic(),
                next_value->id());
        } else {
          TRACE("  input %i: nullptr\n", i);
        }
        buffer_.push_back(next_value);
      }

      Type* phi_type = Type::None();
      for (Node* input : buffer_) {
        if (input) {
          Type* input_type = NodeProperties::GetType(input);
          phi_type = Type::Union(phi_type, input_type, graph_->zone());
        }
      }

      Node* old_value = table_[effect_phi][var];
      if (old_value) {
        TRACE("  old: %s#%d\n", old_value->op()->mnemonic(), old_value->id());
      } else {
        TRACE("  old: nullptr\n");
      }
      if (old_value && old_value->opcode() == IrOpcode::kPhi &&
          NodeProperties::GetControlInput(old_value, 0) == control) {
        // Since a phi node can never dominate its control node,
        // [old_value] cannot originate from the inputs. Thus [old_value]
        // must have been created by this reducer.
        for (int i = 0; i < arity; ++i) {
          // This change cannot affect the rest of the reducer, so there is no
          // need to revisit.
          NodeProperties::ReplaceValueInput(
              old_value, buffer_[i] ? buffer_[i] : graph_->Dead(), i);
          NodeProperties::SetType(old_value, phi_type);
        }
        result[var] = old_value;
      } else {
        if (num_defined_inputs == 1 && is_loop) {
          DCHECK(arity == 2);
          result[var] = value;
        } else if (num_defined_inputs < arity) {
          result[var] = nullptr;
        } else {
          DCHECK(num_defined_inputs == arity);
          if (identical_inputs) {
            result[var] = value;
          } else {
            TRACE("Creating new phi\n");
            buffer_.push_back(control);
            Node* phi = graph_->graph()->NewNode(
                graph_->common()->Phi(MachineRepresentation::kTagged, arity),
                arity + 1, &buffer_.front());
            NodeProperties::SetType(phi, phi_type);
            reducer_->AddRoot(phi);
            result[var] = phi;
          }
        }
      }
#ifdef DEBUG
      if (Node* result_node = result[var]) {
        TRACE("  result: %s#%d\n", result_node->op()->mnemonic(),
              result_node->id());
      } else {
        TRACE("  result: nullptr\n");
      }
#endif
    }
  }
  return result;
}

Node* EscapeAnalysisState::GetVirtualObjectField(const VirtualObject* vobject,
                                                 int field, Node* effect) {
  Variable var;
  bool success = vobject->FieldAt(field, &var);
  CHECK(success);
  return variable_states_.Get(var, effect);
}

namespace {

int OffsetOfFieldAccess(const Operator* op) {
  DCHECK(op->opcode() == IrOpcode::kLoadField ||
         op->opcode() == IrOpcode::kStoreField);
  FieldAccess access = FieldAccessOf(op);
  return access.offset;
}

bool OffsetOfElementsAccess(const Operator* op, Node* index_node, int* offset) {
  DCHECK(op->opcode() == IrOpcode::kLoadElement ||
         op->opcode() == IrOpcode::kStoreElement);
  NumberMatcher index(index_node);
  DCHECK(index_node->opcode() != IrOpcode::kInt32Constant &&
         index_node->opcode() != IrOpcode::kInt64Constant &&
         index_node->opcode() != IrOpcode::kFloat32Constant &&
         index_node->opcode() != IrOpcode::kFloat64Constant);
  if (!index.HasValue()) return false;
  ElementAccess access = ElementAccessOf(op);
  DCHECK_GE(ElementSizeLog2Of(access.machine_type.representation()),
            kPointerSizeLog2);
  *offset = access.header_size +
            (static_cast<int>(index.Value())
             << ElementSizeLog2Of(access.machine_type.representation()));
  return true;
}

}  // namespace

void NewEscapeAnalysis::ReduceNode(const Operator* op,
                                   EscapeAnalysisState::Scope* current) {
  switch (op->opcode()) {
    case IrOpcode::kAllocate: {
      NumberMatcher size(current->ValueInput(0));
      if (size.HasValue()) {
        if (const VirtualObject* vobject =
                current->InitVirtualObject(size.Value())) {
          // Initialize with dead nodes as a sentinel for uninitialized memory.
          for (Variable field : *vobject) {
            current->Set(field, jsgraph()->Dead());
          }
        }
      }
    } break;
    case IrOpcode::kFinishRegion:
      current->SetVirtualObject(current->ValueInput(0));
      break;
    case IrOpcode::kStoreField: {
      Node* object = current->ValueInput(0);
      Node* value = current->ValueInput(1);
      const VirtualObject* vobject = current->GetVirtualObject(object);
      Variable var;
      if (vobject && !vobject->HasEscaped() &&
          vobject->FieldAt(OffsetOfFieldAccess(op), &var)) {
        current->Set(var, value);
        current->SetCurrentReplacement(jsgraph()->Dead());
      } else {
        current->SetEscaped(object);
        current->SetEscaped(value);
      }
    } break;
    case IrOpcode::kStoreElement: {
      Node* object = current->ValueInput(0);
      Node* index = current->ValueInput(1);
      Node* value = current->ValueInput(2);
      const VirtualObject* vobject = current->GetVirtualObject(object);
      int offset;
      Variable var;
      if (vobject && !vobject->HasEscaped() &&
          OffsetOfElementsAccess(op, index, &offset) &&
          vobject->FieldAt(offset, &var)) {
        current->Set(var, value);
        current->SetCurrentReplacement(jsgraph()->Dead());
      } else {
        current->SetEscaped(value);
        current->SetEscaped(object);
      }
    } break;
    case IrOpcode::kLoadField: {
      Node* object = current->ValueInput(0);
      const VirtualObject* vobject = current->GetVirtualObject(object);
      Variable var;
      if (vobject && !vobject->HasEscaped() &&
          vobject->FieldAt(OffsetOfFieldAccess(op), &var)) {
        current->SetCurrentReplacement(current->Get(var));
      } else {
        // TODO(tebbi): At the moment, we mark objects as escaping if there
        // is a load from an invalid location to avoid dead nodes. This is a
        // workaround that should be removed once we can handle dead nodes
        // everywhere.
        current->SetEscaped(object);
      }
    } break;
    case IrOpcode::kLoadElement: {
      Node* object = current->ValueInput(0);
      Node* index = current->ValueInput(1);
      const VirtualObject* vobject = current->GetVirtualObject(object);
      int offset;
      Variable var;
      if (vobject && !vobject->HasEscaped() &&
          OffsetOfElementsAccess(op, index, &offset) &&
          vobject->FieldAt(offset, &var)) {
        current->SetCurrentReplacement(current->Get(var));
      } else {
        current->SetEscaped(object);
      }
    } break;
    case IrOpcode::kTypeGuard: {
      // The type-guard is re-introduced in the final reducer if the types
      // don't match.
      current->SetCurrentReplacement(current->ValueInput(0));
    } break;
    case IrOpcode::kReferenceEqual: {
      Node* left = current->ValueInput(0);
      Node* right = current->ValueInput(1);
      const VirtualObject* left_object = current->GetVirtualObject(left);
      const VirtualObject* right_object = current->GetVirtualObject(right);
      if (left_object && !left_object->HasEscaped()) {
        if (right_object && !right_object->HasEscaped() &&
            left_object->id() == right_object->id()) {
          current->SetCurrentReplacement(jsgraph()->TrueConstant());
        } else {
          current->SetCurrentReplacement(jsgraph()->FalseConstant());
        }
      } else if (right_object && !right_object->HasEscaped()) {
        current->SetCurrentReplacement(jsgraph()->FalseConstant());
      }
    } break;
    case IrOpcode::kStateValues:
    case IrOpcode::kFrameState:
      // safe usages
      break;
    default: {
      int value_input_count = op->ValueInputCount();
      for (int i = 0; i < value_input_count; ++i) {
        Node* input = current->ValueInput(i);
        current->SetEscaped(input);
      }
      if (OperatorProperties::HasContextInput(op)) {
        current->SetEscaped(current->ContextInput());
      }
    } break;
  }
}

NewEscapeAnalysis::Reduction NewEscapeAnalysis::Reduce(Node* node) {
  const Operator* op = node->op();
  TRACE("Reducing %s#%d\n", op->mnemonic(), node->id());

  Reduction reduction;
  EscapeAnalysisState::Scope current(this, node, &reduction);
  ReduceNode(op, &current);
  return reduction;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
