// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/new-escape-analysis-reducer.h"

#include "src/compiler/all-nodes.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/type-cache.h"

namespace v8 {
namespace internal {
namespace compiler {

#ifdef DEBUG
#define TRACE(...)                                    \
  do {                                                \
    if (FLAG_trace_turbo_escape) PrintF(__VA_ARGS__); \
  } while (false)
#else
#define TRACE(...)
#endif  // DEBUG

NewEscapeAnalysisReducer::NewEscapeAnalysisReducer(
    Editor* editor, JSGraph* jsgraph, NewEscapeAnalysis* escape_analysis,
    Zone* zone)
    : AdvancedReducer(editor),
      jsgraph_(jsgraph),
      escape_analysis_(escape_analysis),
      object_id_cache_(zone),
      node_cache_(jsgraph->graph(), zone),
      arguments_elements_(zone),
      zone_(zone) {}

Node* NewEscapeAnalysisReducer::MaybeGuard(Node* original, Node* replacement) {
  // We might need to guard the replacement if the type of the {replacement}
  // node is not in a sub-type relation to the type of the the {original} node.
  Type* const replacement_type = NodeProperties::GetType(replacement);
  Type* const original_type = NodeProperties::GetType(original);
  if (!replacement_type->Is(original_type)) {
    Node* const control = NodeProperties::GetControlInput(original);
    replacement = jsgraph()->graph()->NewNode(
        jsgraph()->common()->TypeGuard(original_type), replacement, control);
    NodeProperties::SetType(replacement, original_type);
  }
  return replacement;
}

Node* NewEscapeAnalysisReducer::ObjectIdNode(const VirtualObject* vobject) {
  VirtualObject::Id id = vobject->id();
  if (id >= object_id_cache_.size()) object_id_cache_.resize(id + 1);
  if (!object_id_cache_[id]) {
    Node* node = jsgraph()->graph()->NewNode(jsgraph()->common()->ObjectId(id));
    NodeProperties::SetType(node, Type::Object());
    object_id_cache_[id] = node;
  }
  return object_id_cache_[id];
}

Reduction NewEscapeAnalysisReducer::Reduce(Node* node) {
  if (Node* replacement = escape_analysis()->GetReplacementOf(node)) {
    DCHECK(node->opcode() != IrOpcode::kAllocate &&
           node->opcode() != IrOpcode::kFinishRegion);
    RelaxEffectsAndControls(node);
    if (replacement != jsgraph()->Dead()) {
      replacement = MaybeGuard(node, replacement);
    }
    RelaxEffectsAndControls(node);
    return Replace(replacement);
  }

  switch (node->opcode()) {
    case IrOpcode::kAllocate: {
      const VirtualObject* vobject = escape_analysis()->GetVirtualObject(node);
      if (vobject && !vobject->HasEscaped()) {
        RelaxEffectsAndControls(node);
      }
      return NoChange();
    }
    case IrOpcode::kFinishRegion: {
      Node* effect = NodeProperties::GetEffectInput(node, 0);
      if (effect->opcode() == IrOpcode::kBeginRegion) {
        RelaxEffectsAndControls(effect);
        RelaxEffectsAndControls(node);
      }
      return NoChange();
    }
    case IrOpcode::kNewUnmappedArgumentsElements:
      arguments_elements_.insert(node);
      return NoChange();
    default: {
      // TODO(sigurds): Change this to GetFrameStateInputCount once
      // it is working. For now we use EffectInputCount > 0 to determine
      // whether a node might have a frame state input.
      if (node->op()->EffectInputCount() > 0) {
        ReduceFrameStateInputs(node);
      }
      return NoChange();
    }
  }
}

class Deduplicator {
 public:
  explicit Deduplicator(Zone* zone) : is_duplicate_(zone) {}
  bool SeenBefore(const VirtualObject* vobject) {
    VirtualObject::Id id = vobject->id();
    if (id >= is_duplicate_.size()) {
      is_duplicate_.resize(id + 1);
    }
    bool is_duplicate = is_duplicate_[id];
    is_duplicate_[id] = true;
    return is_duplicate;
  }

 private:
  ZoneVector<bool> is_duplicate_;
};

void NewEscapeAnalysisReducer::ReduceFrameStateInputs(Node* node) {
  DCHECK_GE(node->op()->EffectInputCount(), 1);
  for (int i = 0; i < node->InputCount(); ++i) {
    Node* input = node->InputAt(i);
    if (input->opcode() == IrOpcode::kFrameState) {
      Deduplicator deduplicator(zone());
      if (Node* ret = ReduceDeoptState(input, node, &deduplicator)) {
        node->ReplaceInput(i, ret);
      }
    }
  }
}

Node* NewEscapeAnalysisReducer::ReduceDeoptState(Node* node, Node* effect,
                                                 Deduplicator* deduplicator) {
  TRACE_FN("ReduceDeoptState", node);
  if (node->opcode() == IrOpcode::kFrameState) {
    NodeHashCache::Constructor new_node(&node_cache_, node);
    // This input order is important to match the DFS traversal used in the
    // instruction selector. Otherwise, the instruction selector might find a
    // duplicate node before the original one.
    for (int input_id : {kFrameStateOuterStateInput, kFrameStateFunctionInput,
                         kFrameStateParametersInput, kFrameStateContextInput,
                         kFrameStateLocalsInput, kFrameStateStackInput}) {
      Node* input = node->InputAt(input_id);
      new_node.ReplaceInput(ReduceDeoptState(input, effect, deduplicator),
                            input_id);
    }
    return new_node.Get();
  } else if (node->opcode() == IrOpcode::kStateValues) {
    NodeHashCache::Constructor new_node(&node_cache_, node);
    for (int i = 0; i < node->op()->ValueInputCount(); ++i) {
      Node* input = NodeProperties::GetValueInput(node, i);
      new_node.ReplaceValueInput(ReduceDeoptState(input, effect, deduplicator),
                                 i);
    }
    return new_node.Get();
  } else if (const VirtualObject* vobject =
                 escape_analysis()->GetVirtualObject(node)) {
    if (vobject->HasEscaped()) return node;
    if (deduplicator->SeenBefore(vobject)) {
      return ObjectIdNode(vobject);
    } else {
      std::vector<Node*> inputs;
      for (int offset = 0; offset < vobject->size(); offset += kPointerSize) {
        Node* field =
            escape_analysis()->GetVirtualObjectField(vobject, offset, effect);
        CHECK_NOT_NULL(field);
        if (field != jsgraph()->Dead()) {
          inputs.push_back(ReduceDeoptState(field, effect, deduplicator));
        }
      }
      int num_inputs = static_cast<int>(inputs.size());
      NodeHashCache::Constructor new_node(
          &node_cache_,
          jsgraph()->common()->ObjectState(vobject->id(), num_inputs),
          num_inputs, &inputs.front(), NodeProperties::GetType(node));
      return new_node.Get();
    }
  } else {
    return node;
  }
}

void NewEscapeAnalysisReducer::VerifyReplacement() const {
#ifdef DEBUG
  AllNodes all(zone(), jsgraph()->graph());
  for (Node* node : all.reachable) {
    if (node->opcode() == IrOpcode::kAllocate) {
      if (const VirtualObject* vobject =
              escape_analysis_->GetVirtualObject(node)) {
        if (!vobject->HasEscaped()) {
          V8_Fatal(__FILE__, __LINE__,
                   "Escape analysis failed to remove node %s#%d\n",
                   node->op()->mnemonic(), node->id());
        }
      }
    }
  }
#endif  // DEBUG
}

void NewEscapeAnalysisReducer::Finalize() {
  for (Node* node : arguments_elements_) {
    DCHECK(node->opcode() == IrOpcode::kNewUnmappedArgumentsElements);

    Node* arguments_frame = NodeProperties::GetValueInput(node, 0);
    if (arguments_frame->opcode() != IrOpcode::kArgumentsFrame) continue;
    Node* arguments_length = NodeProperties::GetValueInput(node, 1);
    if (arguments_length->opcode() != IrOpcode::kArgumentsLength) continue;

    Node* arguments_length_state = nullptr;
    for (Edge edge : arguments_length->use_edges()) {
      Node* use = edge.from();
      switch (use->opcode()) {
        case IrOpcode::kObjectState:
        case IrOpcode::kTypedObjectState:
        case IrOpcode::kStateValues:
        case IrOpcode::kTypedStateValues:
          if (!arguments_length_state) {
            arguments_length_state = jsgraph()->graph()->NewNode(
                jsgraph()->common()->ArgumentsLengthState(
                    IsRestLengthOf(arguments_length->op())));
            NodeProperties::SetType(arguments_length_state,
                                    Type::OtherInternal());
          }
          edge.UpdateTo(arguments_length_state);
          break;
        default:
          break;
      }
    }

    bool escaping_use = false;
    ZoneVector<Node*> loads(zone());
    for (Edge edge : node->use_edges()) {
      Node* use = edge.from();
      if (!NodeProperties::IsValueEdge(edge)) continue;
      if (use->use_edges().empty()) {
        // A node without uses is dead, so we don't have to care about it.
        continue;
      }
      switch (use->opcode()) {
        case IrOpcode::kStateValues:
        case IrOpcode::kTypedStateValues:
        case IrOpcode::kObjectState:
        case IrOpcode::kTypedObjectState:
          break;
        case IrOpcode::kLoadElement:
          loads.push_back(use);
          break;
        case IrOpcode::kLoadField:
          if (FieldAccessOf(use->op()).offset == FixedArray::kLengthOffset) {
            loads.push_back(use);
          } else {
            escaping_use = true;
          }
          break;
        default:
          // If the arguments elements node node is used by an unhandled node,
          // then we cannot remove this allocation.
          escaping_use = true;
          break;
      }
      if (escaping_use) break;
    }
    if (!escaping_use) {
      Node* arguments_elements_state = jsgraph()->graph()->NewNode(
          jsgraph()->common()->ArgumentsElementsState(
              IsRestLengthOf(arguments_length->op())));
      NodeProperties::SetType(arguments_elements_state, Type::OtherInternal());
      ReplaceWithValue(node, arguments_elements_state);

      ElementAccess stack_access;
      stack_access.base_is_tagged = BaseTaggedness::kUntaggedBase;
      // Reduce base address by {kPointerSize} such that (length - index)
      // resolves to the right position.
      stack_access.header_size =
          CommonFrameConstants::kFixedFrameSizeAboveFp - kPointerSize;
      stack_access.type = Type::NonInternal();
      stack_access.machine_type = MachineType::AnyTagged();
      stack_access.write_barrier_kind = WriteBarrierKind::kNoWriteBarrier;
      const Operator* load_stack_op =
          jsgraph()->simplified()->LoadElement(stack_access);

      for (Node* load : loads) {
        switch (load->opcode()) {
          case IrOpcode::kLoadElement: {
            Node* index = NodeProperties::GetValueInput(load, 1);
            // {offset} is a reverted index starting from 1. The base address is
            // adapted to allow offsets starting from 1.
            Node* offset = jsgraph()->graph()->NewNode(
                jsgraph()->simplified()->NumberSubtract(), arguments_length,
                index);
            NodeProperties::SetType(offset,
                                    TypeCache::Get().kArgumentsLengthType);
            NodeProperties::ReplaceValueInput(load, arguments_frame, 0);
            NodeProperties::ReplaceValueInput(load, offset, 1);
            NodeProperties::ChangeOp(load, load_stack_op);
            break;
          }
          case IrOpcode::kLoadField: {
            DCHECK_EQ(FieldAccessOf(load->op()).offset,
                      FixedArray::kLengthOffset);
            Node* length = NodeProperties::GetValueInput(node, 1);
            ReplaceWithValue(load, length);
            break;
          }
          default:
            UNREACHABLE();
        }
      }
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
