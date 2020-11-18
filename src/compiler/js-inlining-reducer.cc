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
    case IrOpcode::kJSStoreNamed:
      return ReduceJSStoreNamed(node);
    case IrOpcode::kJSHasProperty:
      return ReduceJSHasProperty(node);
    case IrOpcode::kJSLoadProperty:
      return ReduceJSLoadProperty(node);
    case IrOpcode::kJSStoreProperty:
      return ReduceJSStoreProperty(node);
    case IrOpcode::kJSStoreNamedOwn:
      return ReduceJSStoreNamedOwn(node);
    case IrOpcode::kJSStoreDataPropertyInLiteral:
      return ReduceJSStoreDataPropertyInLiteral(node);
    case IrOpcode::kJSStoreInArrayLiteral:
      return ReduceJSStoreInArrayLiteral(node);
    case IrOpcode::kJSLoadGlobal:
    case IrOpcode::kJSStoreGlobal:
    case IrOpcode::kJSDeleteProperty:
      return NoChange();
    default:
      UNREACHABLE();
  }
}

Reduction JSInliningReducer::ReduceJSLoadNamed(Node* node) {
  JSLoadNamedNode n(node);
  NamedAccess const& p = n.Parameters();
  return ReducePropertyAccess(node, nullptr, NameRef{broker(), p.name()},
                              jsgraph()->Dead(), p.feedback(),
                              AccessMode::kLoad);
}

Reduction JSInliningReducer::ReduceJSLoadNamedFromSuper(Node* node) {
  NamedAccess const& p = JSLoadNamedFromSuperNode{node}.Parameters();
  return ReducePropertyAccess(node, nullptr, NameRef{broker(), p.name()},
                              jsgraph()->Dead(), p.feedback(),
                              AccessMode::kLoad);
}

Reduction JSInliningReducer::ReduceJSStoreNamed(Node* node) {
  JSStoreNamedNode n(node);
  NamedAccess const& p = n.Parameters();
  return ReducePropertyAccess(node, nullptr, NameRef(broker(), p.name()),
                              n.value(), p.feedback(), AccessMode::kStore);
}

Reduction JSInliningReducer::ReduceJSStoreNamedOwn(Node* node) {
  JSStoreNamedOwnNode n(node);
  StoreNamedOwnParameters const& p = n.Parameters();
  return ReducePropertyAccess(node, nullptr, NameRef(broker(), p.name()),
                              n.value(), p.feedback(),
                              AccessMode::kStoreInLiteral);
}

Reduction JSInliningReducer::ReduceJSHasProperty(Node* node) {
  JSHasPropertyNode n(node);
  PropertyAccess const& p = n.Parameters();
  return ReducePropertyAccess(node, n.key(), base::nullopt, jsgraph()->Dead(),
                              p.feedback(), AccessMode::kHas);
}

Reduction JSInliningReducer::ReduceJSLoadProperty(Node* node) {
  JSLoadPropertyNode n(node);
  PropertyAccess const& p = n.Parameters();
  return ReducePropertyAccess(node, n.key(), base::nullopt, jsgraph()->Dead(),
                              p.feedback(), AccessMode::kLoad);
}

Reduction JSInliningReducer::ReduceJSStoreProperty(Node* node) {
  JSStorePropertyNode n(node);
  PropertyAccess const& p = n.Parameters();
  return ReducePropertyAccess(node, n.key(), base::nullopt, n.value(),
                              p.feedback(), AccessMode::kStore);
}

Reduction JSInliningReducer::ReduceJSStoreDataPropertyInLiteral(Node* node) {
  JSStoreDataPropertyInLiteralNode n(node);
  FeedbackParameter const& p = n.Parameters();

  NumberMatcher mflags(n.flags());
  CHECK(mflags.HasResolvedValue());
  DataPropertyInLiteralFlags cflags(mflags.ResolvedValue());
  DCHECK(!(cflags & DataPropertyInLiteralFlag::kDontEnum));
  if (cflags & DataPropertyInLiteralFlag::kSetFunctionName) return NoChange();

  return ReducePropertyAccess(node, n.name(), base::nullopt, n.value(),
                              p.feedback(), AccessMode::kStoreInLiteral);
}

Reduction JSInliningReducer::ReduceJSStoreInArrayLiteral(Node* node) {
  JSStoreInArrayLiteralNode n(node);
  FeedbackParameter const& p = n.Parameters();
  return ReducePropertyAccess(node, n.index(), base::nullopt, n.value(),
                              p.feedback(), AccessMode::kStoreInLiteral);
}

Reduction JSInliningReducer::ReducePropertyAccess(
    Node* node, Node* key, base::Optional<NameRef> static_name, Node* value,
    FeedbackSource const& source, AccessMode access_mode) {
  if (!source.IsValid()) return NoChange();

  DisallowHeapAccessIf disallow_heap_access(broker()->is_concurrent_inlining());

  DCHECK_EQ(key == nullptr, static_name.has_value());
  DCHECK(node->opcode() == IrOpcode::kJSLoadProperty ||
         node->opcode() == IrOpcode::kJSStoreProperty ||
         node->opcode() == IrOpcode::kJSStoreInArrayLiteral ||
         node->opcode() == IrOpcode::kJSStoreDataPropertyInLiteral ||
         node->opcode() == IrOpcode::kJSHasProperty ||
         node->opcode() == IrOpcode::kJSLoadNamed ||
         node->opcode() == IrOpcode::kJSStoreNamed ||
         node->opcode() == IrOpcode::kJSStoreNamedOwn ||
         node->opcode() == IrOpcode::kJSLoadNamedFromSuper);
  DCHECK_GE(node->op()->ControlOutputCount(), 1);

  ProcessedFeedback const& feedback =
      broker()->GetFeedbackForPropertyAccess(source, access_mode, static_name);
  if (feedback.kind() != ProcessedFeedback::kMinimorphicPropertyAccess) {
    return NoChange();  // Handled in JSNativeContextSpecialization.
  }

  // This stricter check says which opcodes we expect to have minimorphic
  // feedback for.
  DCHECK(node->opcode() == IrOpcode::kJSLoadNamed ||
         node->opcode() == IrOpcode::kJSLoadNamedFromSuper);

  // Non-minimorphic accesses are lowered in js-native-context-specialization.
  DCHECK_EQ(access_mode, AccessMode::kLoad);
  DCHECK_NULL(key);
  return ReduceMinimorphicPropertyAccess(
      node, value, feedback.AsMinimorphicPropertyAccess(), source);
}

Reduction JSInliningReducer::ReduceMinimorphicPropertyAccess(
    Node* node, Node* value,
    MinimorphicLoadPropertyAccessFeedback const& feedback,
    FeedbackSource const& source) {
  DCHECK(node->opcode() == IrOpcode::kJSLoadNamed ||
         node->opcode() == IrOpcode::kJSLoadProperty ||
         node->opcode() == IrOpcode::kJSLoadNamedFromSuper);
  STATIC_ASSERT(JSLoadNamedNode::ObjectIndex() == 0 &&
                JSLoadPropertyNode::ObjectIndex() == 0);

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
    lookup_start_object = NodeProperties::GetValueInput(node, 0);
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
  value = access_builder.BuildMinimorphicLoadDataField(
      feedback.name(), access_info, lookup_start_object, &effect, &control);

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
