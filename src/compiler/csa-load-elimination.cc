// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/csa-load-elimination.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

Reduction CsaLoadElimination::Reduce(Node* node) {
  if (FLAG_trace_turbo_load_elimination) {
    if (node->op()->EffectInputCount() > 0) {
      PrintF(" visit #%d:%s", node->id(), node->op()->mnemonic());
      if (node->op()->ValueInputCount() > 0) {
        PrintF("(");
        for (int i = 0; i < node->op()->ValueInputCount(); ++i) {
          if (i > 0) PrintF(", ");
          Node* const value = NodeProperties::GetValueInput(node, i);
          PrintF("#%d:%s", value->id(), value->op()->mnemonic());
        }
        PrintF(")");
      }
      PrintF("\n");
      for (int i = 0; i < node->op()->EffectInputCount(); ++i) {
        Node* const effect = NodeProperties::GetEffectInput(node, i);
        if (AbstractState const* const state = node_states_.Get(effect)) {
          PrintF("  state[%i]: #%d:%s\n", i, effect->id(),
                 effect->op()->mnemonic());
          state->Print();
        } else {
          PrintF("  no state[%i]: #%d:%s\n", i, effect->id(),
                 effect->op()->mnemonic());
        }
      }
    }
  }
  switch (node->opcode()) {
    case IrOpcode::kLoadFromObject:
      return ReduceLoadFromObject(node, ObjectAccessOf(node->op()));
    case IrOpcode::kStoreToObject:
      return ReduceStoreToObject(node, ObjectAccessOf(node->op()));
    case IrOpcode::kDebugBreak:
    case IrOpcode::kAbortCSAAssert:
      // Avoid changing optimizations in the presence of debug instructions.
      return PropagateInputState(node);
    case IrOpcode::kCall:
      return ReduceCall(node);
    case IrOpcode::kEffectPhi:
      return ReduceEffectPhi(node);
    case IrOpcode::kDead:
      return NoChange();
    case IrOpcode::kStart:
      return ReduceStart(node);
    default:
      return ReduceOtherNode(node);
  }
  UNREACHABLE();
}

namespace CsaLoadEliminationHelpers {

bool Subsumes(MachineRepresentation from, MachineRepresentation to) {
  if (from == to) return true;
  if (IsAnyTagged(from)) return IsAnyTagged(to);
  if (IsIntegral(from)) {
    return IsIntegral(to) && ElementSizeInBytes(from) >= ElementSizeInBytes(to);
  }
  return false;
}

bool IsConstantObject(Node* object) {
  return object->opcode() == IrOpcode::kParameter ||
         object->opcode() == IrOpcode::kLoadImmutable ||
         NodeProperties::IsConstant(object);
}

bool IsFreshObject(Node* object) {
  return NodeProperties::IsFreshObject(object);
}

}  // namespace CsaLoadEliminationHelpers

namespace Helpers = CsaLoadEliminationHelpers;

// static
template <typename OuterKey>
void CsaLoadElimination::AbstractState::IntersectWith(
    OuterMap<OuterKey>& to, const OuterMap<OuterKey>& from) {
  FieldInfo empty_info;
  for (const std::pair<OuterKey, InnerMap>& to_map : to) {
    InnerMap to_map_copy(to_map.second);
    OuterKey key = to_map.first;
    for (std::pair<Node*, FieldInfo> info : to_map.second) {
      if (from.Get(key).Get(info.first) != info.second) {
        to_map_copy.Set(info.first, empty_info);
      }
    }
    to.Set(key, to_map_copy);
  }
}

void CsaLoadElimination::AbstractState::IntersectWith(AbstractState const* that,
                                                      Zone* zone) {
  IntersectWith(fresh_entries_, that->fresh_entries_);
  IntersectWith(constant_entries_, that->constant_entries_);
  IntersectWith(arbitrary_entries_, that->arbitrary_entries_);
  IntersectWith(fresh_unknown_entries_, that->fresh_unknown_entries_);
  IntersectWith(constant_unknown_entries_, that->constant_unknown_entries_);
  IntersectWith(arbitrary_unknown_entries_, that->arbitrary_unknown_entries_);
}

CsaLoadElimination::AbstractState const*
CsaLoadElimination::AbstractState::KillField(Node* kill_object,
                                             Node* kill_offset,
                                             MachineRepresentation kill_repr,
                                             Zone* zone) const {
  AbstractState* result = zone->New<AbstractState>(*this);
  UnknownOffsetInfos empty_unknown(zone, InnerMap(zone));
  IntPtrMatcher m(kill_offset);
  if (m.HasResolvedValue()) {
    uint32_t num_offset = static_cast<uint32_t>(m.ResolvedValue());
    if (Helpers::IsFreshObject(kill_object)) {
      // May alias with:
      // - The same object/offset
      // - Arbitrary objects with the same offset
      // - The same object, unkwown offset
      // - Arbitrary objects with unkwown offset
      result->KillFreshOffset(kill_object, num_offset, kill_repr);
      KillOffset(result->arbitrary_entries_, num_offset, kill_repr, zone);
      result->fresh_unknown_entries_.Set(kill_object, InnerMap(zone));
      result->arbitrary_unknown_entries_ = empty_unknown;
    } else if (Helpers::IsConstantObject(kill_object)) {
      // May alias with:
      // - Constant/arbitrary objects with the same offset
      // - Constant/arbitrary objects with unkwown offset
      KillOffset(result->constant_entries_, num_offset, kill_repr, zone);
      KillOffset(result->arbitrary_entries_, num_offset, kill_repr, zone);
      result->constant_unknown_entries_ = empty_unknown;
      result->arbitrary_unknown_entries_ = empty_unknown;
    } else {
      // May alias with:
      // - Any object with the same or unknown offset
      KillOffset(result->fresh_entries_, num_offset, kill_repr, zone);
      KillOffset(result->constant_entries_, num_offset, kill_repr, zone);
      KillOffset(result->arbitrary_entries_, num_offset, kill_repr, zone);
      result->fresh_unknown_entries_ = empty_unknown;
      result->constant_unknown_entries_ = empty_unknown;
      result->arbitrary_unknown_entries_ = empty_unknown;
    }
  } else {
    ConstantOffsetInfos empty_constant(zone, InnerMap(zone));
    if (Helpers::IsFreshObject(kill_object)) {
      // May alias with:
      // - The same object with any known/unknown offset
      // - Arbitrary objects with any known/unknown offset
      for (auto map : result->fresh_entries_) {
        // TODO(manoskouk): Consider adding a map from fresh objects to offsets
        // to implement this efficiently.
        InnerMap map_copy(map.second);
        map_copy.Set(kill_object, FieldInfo());
        result->fresh_entries_.Set(map.first, map_copy);
      }
      result->fresh_unknown_entries_.Set(kill_object, InnerMap(zone));
      result->arbitrary_entries_ = empty_constant;
      result->arbitrary_unknown_entries_ = empty_unknown;
    } else if (Helpers::IsConstantObject(kill_object)) {
      // May alias with:
      // - Constant/arbitrary objects with the any known/unknown offset
      result->constant_entries_ = empty_constant;
      result->constant_unknown_entries_ = empty_unknown;
      result->arbitrary_entries_ = empty_constant;
      result->arbitrary_unknown_entries_ = empty_unknown;
    } else {
      // May alias with anything. Clear the state.
      return zone->New<AbstractState>(zone);
    }
  }

  return result;
}

CsaLoadElimination::AbstractState const*
CsaLoadElimination::AbstractState::AddField(Node* object, Node* offset,
                                            Node* value,
                                            MachineRepresentation repr,
                                            Zone* zone) const {
  AbstractState* new_state = zone->New<AbstractState>(*this);
  IntPtrMatcher m(offset);
  if (m.HasResolvedValue()) {
    uint32_t offset_num = static_cast<uint32_t>(m.ResolvedValue());
    ConstantOffsetInfos& infos = Helpers::IsFreshObject(object)
                                     ? new_state->fresh_entries_
                                     : Helpers::IsConstantObject(object)
                                           ? new_state->constant_entries_
                                           : new_state->arbitrary_entries_;
    InnerMap map_copy(infos.Get(offset_num));
    map_copy.Set(object, FieldInfo(value, repr));
    infos.Set(offset_num, map_copy);
  } else {
    UnknownOffsetInfos& infos =
        Helpers::IsFreshObject(object)
            ? new_state->fresh_unknown_entries_
            : Helpers::IsConstantObject(object)
                  ? new_state->constant_unknown_entries_
                  : new_state->arbitrary_unknown_entries_;
    InnerMap map_copy(infos.Get(object));
    map_copy.Set(offset, FieldInfo(value, repr));
    infos.Set(object, map_copy);
  }
  return new_state;
}

CsaLoadElimination::FieldInfo CsaLoadElimination::AbstractState::Lookup(
    Node* object, Node* offset) const {
  IntPtrMatcher m(offset);
  if (m.HasResolvedValue()) {
    uint32_t num_offset = static_cast<uint32_t>(m.ResolvedValue());
    const ConstantOffsetInfos& infos = Helpers::IsFreshObject(object)
                                           ? fresh_entries_
                                           : Helpers::IsConstantObject(object)
                                                 ? constant_entries_
                                                 : arbitrary_entries_;
    return infos.Get(num_offset).Get(object);
  } else {
    const UnknownOffsetInfos& infos = Helpers::IsFreshObject(object)
                                          ? fresh_unknown_entries_
                                          : Helpers::IsConstantObject(object)
                                                ? constant_unknown_entries_
                                                : arbitrary_unknown_entries_;
    return infos.Get(object).Get(offset);
  }
}

constexpr uint32_t maximum_repr_size = 8;

// static
void CsaLoadElimination::AbstractState::KillOffset(ConstantOffsetInfos& infos,
                                                   uint32_t offset,
                                                   MachineRepresentation repr,
                                                   Zone* zone) {
  for (int i = 0; i < ElementSizeInBytes(repr); i++) {
    infos.Set(offset + i, InnerMap(zone));
  }
  int initial_offset = offset >= maximum_repr_size - 1
                           ? static_cast<int>(offset - (maximum_repr_size - 1))
                           : 0;
  for (int i = initial_offset; i < static_cast<int>(offset); i++) {
    InnerMap map_copy(infos.Get(i));
    for (const std::pair<Node*, FieldInfo>& info : infos.Get(i)) {
      if (info.second.representation != MachineRepresentation::kNone &&
          ElementSizeInBytes(info.second.representation) >
              static_cast<int>(offset) - i) {
        map_copy.Set(info.first, {});
      }
    }
    infos.Set(i, map_copy);
  }
}

void CsaLoadElimination::AbstractState::KillFreshOffset(
    Node* const object, uint32_t offset, MachineRepresentation repr) {
  for (int i = 0; i < ElementSizeInBytes(repr); i++) {
    InnerMap map_copy(fresh_entries_.Get(offset + i));
    map_copy.Set(object, {});
    fresh_entries_.Set(offset + i, map_copy);
  }
  int initial_offset = offset >= maximum_repr_size - 1
                           ? static_cast<int>(offset - (maximum_repr_size - 1))
                           : 0;
  for (int i = initial_offset; i < static_cast<int>(offset); i++) {
    const FieldInfo& info = fresh_entries_.Get(i).Get(object);
    if (info.representation != MachineRepresentation::kNone &&
        ElementSizeInBytes(info.representation) >
            static_cast<int>(offset) - i) {
      InnerMap map_copy(fresh_entries_.Get(i));
      map_copy.Set(object, {});
      fresh_entries_.Set(i, map_copy);
    }
  }
}

void CsaLoadElimination::AbstractState::Print() const {}

Reduction CsaLoadElimination::ReduceLoadFromObject(Node* node,
                                                   ObjectAccess const& access) {
  Node* object = NodeProperties::GetValueInput(node, 0);
  Node* offset = NodeProperties::GetValueInput(node, 1);
  Node* effect = NodeProperties::GetEffectInput(node);
  AbstractState const* state = node_states_.Get(effect);
  if (state == nullptr) return NoChange();

  MachineRepresentation representation = access.machine_type.representation();
  FieldInfo lookup_result = state->Lookup(object, offset);
  if (!lookup_result.IsEmpty()) {
    // Make sure we don't reuse values that were recorded with a different
    // representation or resurrect dead {replacement} nodes.
    MachineRepresentation from = lookup_result.representation;
    if (Helpers::Subsumes(from, representation) &&
        !lookup_result.value->IsDead()) {
      Node* replacement =
          TruncateAndExtend(lookup_result.value, from, access.machine_type);
      ReplaceWithValue(node, replacement, effect);
      return Replace(replacement);
    }
  }
  state = state->AddField(object, offset, node, representation, zone());

  return UpdateState(node, state);
}

Reduction CsaLoadElimination::ReduceStoreToObject(Node* node,
                                                  ObjectAccess const& access) {
  Node* object = NodeProperties::GetValueInput(node, 0);
  Node* offset = NodeProperties::GetValueInput(node, 1);
  Node* value = NodeProperties::GetValueInput(node, 2);
  Node* effect = NodeProperties::GetEffectInput(node);
  AbstractState const* state = node_states_.Get(effect);
  if (state == nullptr) return NoChange();

  MachineRepresentation repr = access.machine_type.representation();
  state = state->KillField(object, offset, repr, zone());
  state = state->AddField(object, offset, value, repr, zone());

  return UpdateState(node, state);
}

Reduction CsaLoadElimination::ReduceEffectPhi(Node* node) {
  Node* const effect0 = NodeProperties::GetEffectInput(node, 0);
  Node* const control = NodeProperties::GetControlInput(node);
  AbstractState const* state0 = node_states_.Get(effect0);
  if (state0 == nullptr) return NoChange();
  if (control->opcode() == IrOpcode::kLoop) {
    // Here we rely on having only reducible loops:
    // The loop entry edge always dominates the header, so we can just take
    // the state from the first input, and compute the loop state based on it.
    AbstractState const* state = ComputeLoopState(node, state0);
    return UpdateState(node, state);
  }
  DCHECK_EQ(IrOpcode::kMerge, control->opcode());

  // Shortcut for the case when we do not know anything about some input.
  int const input_count = node->op()->EffectInputCount();
  for (int i = 1; i < input_count; ++i) {
    Node* const effect = NodeProperties::GetEffectInput(node, i);
    if (node_states_.Get(effect) == nullptr) return NoChange();
  }

  // Make a copy of the first input's state and intersect it with the state
  // from other inputs.
  // TODO(manoskouk): Consider computing phis for at least a subset of the
  // state.
  AbstractState* state = zone()->New<AbstractState>(*state0);
  for (int i = 1; i < input_count; ++i) {
    Node* const input = NodeProperties::GetEffectInput(node, i);
    state->IntersectWith(node_states_.Get(input), zone());
  }
  return UpdateState(node, state);
}

Reduction CsaLoadElimination::ReduceStart(Node* node) {
  return UpdateState(node, empty_state());
}

Reduction CsaLoadElimination::ReduceCall(Node* node) {
  Node* value = NodeProperties::GetValueInput(node, 0);
  ExternalReferenceMatcher m(value);
  if (m.Is(ExternalReference::check_object_type())) {
    return PropagateInputState(node);
  }
  return ReduceOtherNode(node);
}

Reduction CsaLoadElimination::ReduceOtherNode(Node* node) {
  if (node->op()->EffectInputCount() == 1 &&
      node->op()->EffectOutputCount() == 1) {
    Node* const effect = NodeProperties::GetEffectInput(node);
    AbstractState const* state = node_states_.Get(effect);
    // If we do not know anything about the predecessor, do not propagate just
    // yet because we will have to recompute anyway once we compute the
    // predecessor.
    if (state == nullptr) return NoChange();
    // If this {node} has some uncontrolled side effects, set its state to
    // {empty_state()}, otherwise to its input state.
    return UpdateState(node, node->op()->HasProperty(Operator::kNoWrite)
                                 ? state
                                 : empty_state());
  }
  DCHECK_EQ(0, node->op()->EffectOutputCount());
  return NoChange();
}

Reduction CsaLoadElimination::UpdateState(Node* node,
                                          AbstractState const* state) {
  AbstractState const* original = node_states_.Get(node);
  // Only signal that the {node} has Changed, if the information about {state}
  // has changed wrt. the {original}.
  if (state != original) {
    if (original == nullptr || !state->Equals(original)) {
      node_states_.Set(node, state);
      return Changed(node);
    }
  }
  return NoChange();
}

Reduction CsaLoadElimination::PropagateInputState(Node* node) {
  Node* const effect = NodeProperties::GetEffectInput(node);
  AbstractState const* state = node_states_.Get(effect);
  if (state == nullptr) return NoChange();
  return UpdateState(node, state);
}

CsaLoadElimination::AbstractState const* CsaLoadElimination::ComputeLoopState(
    Node* node, AbstractState const* state) const {
  DCHECK_EQ(node->opcode(), IrOpcode::kEffectPhi);
  ZoneQueue<Node*> queue(zone());
  ZoneSet<Node*> visited(zone());
  visited.insert(node);
  for (int i = 1; i < node->InputCount() - 1; ++i) {
    queue.push(node->InputAt(i));
  }
  while (!queue.empty()) {
    Node* const current = queue.front();
    queue.pop();
    if (visited.insert(current).second) {
      if (!current->op()->HasProperty(Operator::kNoWrite)) {
        return empty_state();
      }
      for (int i = 0; i < current->op()->EffectInputCount(); ++i) {
        queue.push(NodeProperties::GetEffectInput(current, i));
      }
    }
  }
  return state;
}

Node* CsaLoadElimination::TruncateAndExtend(Node* node,
                                            MachineRepresentation from,
                                            MachineType to) {
  DCHECK(Helpers::Subsumes(from, to.representation()));
  DCHECK_GE(ElementSizeInBytes(from), ElementSizeInBytes(to.representation()));

  if (to == MachineType::Int8() || to == MachineType::Int16()) {
    // 1st case: We want to eliminate a signed 8/16-bit load using the value
    // from a previous subsuming load or store. Since that value might be
    // outside 8/16-bit range, we first truncate it accordingly. Then we
    // sign-extend the result to 32-bit.
    DCHECK_EQ(to.semantic(), MachineSemantic::kInt32);
    if (from == MachineRepresentation::kWord64) {
      node = graph()->NewNode(machine()->TruncateInt64ToInt32(), node);
    }
    int shift = 32 - 8 * ElementSizeInBytes(to.representation());
    return graph()->NewNode(machine()->Word32Sar(),
                            graph()->NewNode(machine()->Word32Shl(), node,
                                             jsgraph()->Int32Constant(shift)),
                            jsgraph()->Int32Constant(shift));
  } else if (to == MachineType::Uint8() || to == MachineType::Uint16()) {
    // 2nd case: We want to eliminate an unsigned 8/16-bit load using the value
    // from a previous subsuming load or store. Since that value might be
    // outside 8/16-bit range, we first truncate it accordingly.
    if (from == MachineRepresentation::kWord64) {
      node = graph()->NewNode(machine()->TruncateInt64ToInt32(), node);
    }
    int mask = (1 << 8 * ElementSizeInBytes(to.representation())) - 1;
    return graph()->NewNode(machine()->Word32And(), node,
                            jsgraph()->Int32Constant(mask));
  } else if (from == MachineRepresentation::kWord64 &&
             to.representation() == MachineRepresentation::kWord32) {
    // 3rd case: Truncate 64-bits into 32-bits.
    return graph()->NewNode(machine()->TruncateInt64ToInt32(), node);
  } else {
    // 4th case: No need for truncation.
    DCHECK((from == to.representation() &&
            (from == MachineRepresentation::kWord32 ||
             from == MachineRepresentation::kWord64 || !IsIntegral(from))) ||
           (IsAnyTagged(from) && IsAnyTagged(to.representation())));
    return node;
  }
}

CommonOperatorBuilder* CsaLoadElimination::common() const {
  return jsgraph()->common();
}

MachineOperatorBuilder* CsaLoadElimination::machine() const {
  return jsgraph()->machine();
}

Graph* CsaLoadElimination::graph() const { return jsgraph()->graph(); }

Isolate* CsaLoadElimination::isolate() const { return jsgraph()->isolate(); }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
