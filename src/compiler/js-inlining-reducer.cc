// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-inlining-reducer.h"

#include "src/compiler/access-builder.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/property-access-builder.h"
#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

JSInliningReducer::JSInliningReducer(Editor* editor, JSGraph* jsgraph,
                                     JSHeapBroker* broker)
    : AdvancedReducer(editor), jsgraph_(jsgraph), broker_(broker) {}

Reduction JSInliningReducer::ReduceImpl(Node* node, IrOpcode::Value opcode) {
  DCHECK_EQ(node->opcode(), opcode);
  switch (opcode) {
    case IrOpcode::kJSLoadNamed:
      return ReduceJSLoadNamed(node);
    case IrOpcode::kJSLoadNamedFromSuper:
      return ReduceJSLoadNamedFromSuper(node);
    default:
      UNREACHABLE();
  }
}

Reduction JSInliningReducer::ReduceJSLoadNamed(Node* node) {
  NamedAccess const& p = JSLoadNamedNode{node}.Parameters();
  return ReducePropertyAccess(node, NameRef{broker(), p.name()}, p.feedback());
}

Reduction JSInliningReducer::ReduceJSLoadNamedFromSuper(Node* node) {
  NamedAccess const& p = JSLoadNamedFromSuperNode{node}.Parameters();
  return ReducePropertyAccess(node, NameRef{broker(), p.name()}, p.feedback());
}

Reduction JSInliningReducer::ReducePropertyAccess(
    Node* node, base::Optional<NameRef> static_name,
    FeedbackSource const& source) {
  if (!source.IsValid()) return NoChange();

  DisallowHeapAccessIf disallow_heap_access(broker()->is_concurrent_inlining());

  DCHECK(static_name.has_value());
  DCHECK(node->opcode() == IrOpcode::kJSLoadNamed ||
         node->opcode() == IrOpcode::kJSLoadNamedFromSuper);
  DCHECK_GE(node->op()->ControlOutputCount(), 1);

  ProcessedFeedback const& feedback = broker()->GetFeedbackForPropertyAccess(
      source, AccessMode::kLoad, static_name);
  if (feedback.kind() != ProcessedFeedback::kMinimorphicPropertyAccess) {
    return NoChange();  // Handled in JSNativeContextSpecialization.
  }

  // This stricter check says which opcodes we expect to have minimorphic
  // feedback for.
  DCHECK(node->opcode() == IrOpcode::kJSLoadNamed ||
         node->opcode() == IrOpcode::kJSLoadNamedFromSuper);

  // Non-minimorphic accesses are lowered in js-native-context-specialization.
  return ReduceMinimorphicPropertyAccess(
      node, feedback.AsMinimorphicPropertyAccess(), source);
}

Reduction JSInliningReducer::ReduceMinimorphicPropertyAccess(
    Node* node, MinimorphicLoadPropertyAccessFeedback const& feedback,
    FeedbackSource const& source) {
  DCHECK(node->opcode() == IrOpcode::kJSLoadNamed ||
         node->opcode() == IrOpcode::kJSLoadNamedFromSuper);

  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  Node* lookup_start_object;
  if (node->opcode() == IrOpcode::kJSLoadNamedFromSuper) {
    DCHECK(FLAG_super_ic);
    JSLoadNamedFromSuperNode n(node);
    // Lookup start object is the __proto__ of the home object.
    Node* map = effect =
        graph()->NewNode(simplified()->LoadField(AccessBuilder::ForMap()),
                         n.home_object(), effect, control);
    lookup_start_object = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForMapPrototype()), map, effect,
        control);
  } else {
    lookup_start_object = JSLoadNamedNode{node}.object();
  }

  MinimorphicLoadPropertyAccessInfo access_info =
      broker()->GetPropertyAccessInfo(
          feedback, source,
          broker()->is_concurrent_inlining()
              ? SerializationPolicy::kAssumeSerialized
              : SerializationPolicy::kSerializeIfNeeded);
  if (access_info.IsInvalid()) return NoChange();

  // The dynamic map check operator loads the feedback vector from the
  // function's frame, so we can only use this for non-inlined functions.
  // TODO(rmcilroy): Add support for using a trampoline like LoadICTrampoline
  // and otherwise pass feedback vector explicitly if we need support for
  // inlined functions.
  // TODO(rmcilroy): Ideally we would check whether we are have an inlined frame
  // state here, but there isn't a good way to distinguish inlined from OSR
  // framestates.
  DCHECK(broker()->is_turboprop());

  PropertyAccessBuilder access_builder(jsgraph(), broker(), nullptr);
  CheckMapsFlags flags = CheckMapsFlag::kNone;
  if (feedback.has_migration_target_maps()) {
    flags |= CheckMapsFlag::kTryMigrateInstance;
  }

  ZoneHandleSet<Map> maps;
  for (Handle<Map> map : feedback.maps()) {
    maps.insert(map, graph()->zone());
  }

  effect = graph()->NewNode(
      simplified()->DynamicCheckMaps(flags, feedback.handler(), maps, source),
      lookup_start_object, effect, control);
  Node* value = access_builder.BuildMinimorphicLoadDataField(
      feedback.name(), access_info, lookup_start_object, &effect, &control);

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
