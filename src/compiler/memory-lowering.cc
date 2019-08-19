// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/memory-lowering.h"

#include "src/compiler/js-graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

MemoryLowering::MemoryLowering(JSGraph* jsgraph, Zone* zone,
                               PoisoningMitigationLevel poisoning_level)
    : jsgraph_(jsgraph),
      zone_(zone),
      graph_assembler_(jsgraph, nullptr, nullptr, zone),
      poisoning_level_(poisoning_level) {}

Reduction MemoryLowering::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kAllocate:
      // Allocate nodes were purged from the graph in effect-control
      // linearization.
      UNREACHABLE();
    case IrOpcode::kAllocateRaw:
      return ReduceAllocateRaw(node);
    case IrOpcode::kLoadElement:
      return ReduceLoadElement(node);
    case IrOpcode::kLoadField:
      return ReduceLoadField(node);
    case IrOpcode::kStoreElement:
      return ReduceStoreElement(node);
    case IrOpcode::kStoreField:
      return ReduceStoreField(node);
    default:
      break;
  }
  return NoChange();
}

#define __ gasm()->

Reduction MemoryLowering::ReduceAllocateRaw(Node* node) {
  DCHECK_EQ(IrOpcode::kAllocateRaw, node->opcode());
  Node* value;
  Node* size = node->InputAt(0);
  Node* effect = node->InputAt(1);
  Node* control = node->InputAt(2);

  PretenureFlag pretenure = PretenureFlagOf(node->op());

  gasm()->Reset(effect, control);

  // Propagate tenuring from outer allocations to inner allocations, i.e.
  // when we allocate an object in old space and store a newly allocated
  // child object into the pretenured object, then the newly allocated
  // child object also should get pretenured to old space.
  if (pretenure == TENURED) {
    for (Edge const edge : node->use_edges()) {
      Node* const user = edge.from();
      if (user->opcode() == IrOpcode::kStoreField && edge.index() == 0) {
        Node* const child = user->InputAt(1);
        if (child->opcode() == IrOpcode::kAllocateRaw &&
            PretenureFlagOf(child->op()) == NOT_TENURED) {
          NodeProperties::ChangeOp(child, node->op());
          break;
        }
      }
    }
  } else {
    DCHECK_EQ(NOT_TENURED, pretenure);
    for (Edge const edge : node->use_edges()) {
      Node* const user = edge.from();
      if (user->opcode() == IrOpcode::kStoreField && edge.index() == 1) {
        Node* const parent = user->InputAt(0);
        if (parent->opcode() == IrOpcode::kAllocateRaw &&
            PretenureFlagOf(parent->op()) == TENURED) {
          pretenure = TENURED;
          break;
        }
      }
    }
  }

  // Determine the top/limit addresses.
  Node* top_address = __ ExternalConstant(
      pretenure == NOT_TENURED
          ? ExternalReference::new_space_allocation_top_address(isolate())
          : ExternalReference::old_space_allocation_top_address(isolate()));
  Node* limit_address = __ ExternalConstant(
      pretenure == NOT_TENURED
          ? ExternalReference::new_space_allocation_limit_address(isolate())
          : ExternalReference::old_space_allocation_limit_address(isolate()));

  Int32Matcher m(size);
  if (m.HasValue() && m.Value() < kMaxRegularHeapObjectSize) {
    int32_t const object_size = m.Value();
    auto call_runtime = __ MakeDeferredLabel();
    auto done = __ MakeLabel(MachineType::PointerRepresentation());

    // Setup a mutable reservation size node; will be patched as we fold
    // additional allocations into this new group.
    Node* size = __ UniqueInt32Constant(object_size);

    // Load allocation top and limit.
    Node* top =
        __ Load(MachineType::Pointer(), top_address, __ IntPtrConstant(0));
    Node* limit =
        __ Load(MachineType::Pointer(), limit_address, __ IntPtrConstant(0));

    // Check if we need to collect garbage before we can start bump pointer
    // allocation (always done for folded allocations).
    Node* check = __ UintLessThan(
        __ IntAdd(top,
                  machine()->Is64() ? __ ChangeInt32ToInt64(size) : size),
        limit);

    __ GotoIfNot(check, &call_runtime);
    __ Goto(&done, top);

    __ Bind(&call_runtime);
    {
      Node* target =
          pretenure == NOT_TENURED ? __ AllocateInNewSpaceStubConstant()
                                    : __
                                      AllocateInOldSpaceStubConstant();
      if (!allocate_operator_.is_set()) {
        auto call_descriptor =
            Linkage::GetAllocateCallDescriptor(graph()->zone());
        allocate_operator_.set(common()->Call(call_descriptor));
      }
      Node* vfalse = __ Call(allocate_operator_.get(), target, size);
      vfalse = __ IntSub(vfalse, __ IntPtrConstant(kHeapObjectTag));
      __ Goto(&done, vfalse);
    }

    __ Bind(&done);

    // Compute the new top and write it back.
    top = __ IntAdd(done.PhiAt(0), __ IntPtrConstant(object_size));
    __ Store(StoreRepresentation(MachineType::PointerRepresentation(),
                                  kNoWriteBarrier),
              top_address, __ IntPtrConstant(0), top);

    // Compute the initial object address.
    value = __ BitcastWordToTagged(
        __ IntAdd(done.PhiAt(0), __ IntPtrConstant(kHeapObjectTag)));

  } else {
    auto call_runtime = __ MakeDeferredLabel();
    auto done = __ MakeLabel(MachineRepresentation::kTaggedPointer);

    // Load allocation top and limit.
    Node* top =
        __ Load(MachineType::Pointer(), top_address, __ IntPtrConstant(0));
    Node* limit =
        __ Load(MachineType::Pointer(), limit_address, __ IntPtrConstant(0));

    // Compute the new top.
    Node* new_top =
        __ IntAdd(top, machine()->Is64() ? __ ChangeInt32ToInt64(size) : size);

    // Check if we can do bump pointer allocation here.
    Node* check = __ UintLessThan(new_top, limit);
    __ GotoIfNot(check, &call_runtime);
    __ Store(StoreRepresentation(MachineType::PointerRepresentation(),
                                 kNoWriteBarrier),
             top_address, __ IntPtrConstant(0), new_top);
    __ Goto(&done, __ BitcastWordToTagged(
                       __ IntAdd(top, __ IntPtrConstant(kHeapObjectTag))));

    __ Bind(&call_runtime);
    Node* target =
        pretenure == NOT_TENURED ? __ AllocateInNewSpaceStubConstant()
                                 : __
                                   AllocateInOldSpaceStubConstant();
    if (!allocate_operator_.is_set()) {
      auto call_descriptor =
          Linkage::GetAllocateCallDescriptor(graph()->zone());
      allocate_operator_.set(common()->Call(call_descriptor));
    }
    __ Goto(&done, __ Call(allocate_operator_.get(), target, size));

    __ Bind(&done);
    value = done.PhiAt(0);
  }
  effect = __ ExtractCurrentEffect();
  control = __ ExtractCurrentControl();

  // Replace all effect uses of {node} with the {effect}, enqueue the
  // effect uses for further processing, and replace all value uses of
  // {node} with the {value}.
  for (Edge edge : node->use_edges()) {
    if (NodeProperties::IsEffectEdge(edge)) {
      edge.UpdateTo(effect);
    } else if (NodeProperties::IsValueEdge(edge)) {
      edge.UpdateTo(value);
    } else {
      DCHECK(NodeProperties::IsControlEdge(edge));
      edge.UpdateTo(control);
    }
  }

  // Kill the {node} to make sure we don't leave dangling dead uses.
  node->Kill();

  return Replace(value);
}

#undef __

Reduction MemoryLowering::ReduceLoadElement(Node* node) {
  DCHECK_EQ(IrOpcode::kLoadElement, node->opcode());
  ElementAccess const& access = ElementAccessOf(node->op());
  Node* index = node->InputAt(1);
  node->ReplaceInput(1, ComputeIndex(access, index));
  if (NeedsPoisoning(access.load_sensitivity) &&
      access.machine_type.representation() !=
          MachineRepresentation::kTaggedPointer) {
    NodeProperties::ChangeOp(node,
                             machine()->PoisonedLoad(access.machine_type));
  } else {
    NodeProperties::ChangeOp(node, machine()->Load(access.machine_type));
  }
  return Changed(node);
}

Reduction MemoryLowering::ReduceLoadField(Node* node) {
  DCHECK_EQ(IrOpcode::kLoadField, node->opcode());
  FieldAccess const& access = FieldAccessOf(node->op());
  Node* offset = jsgraph()->IntPtrConstant(access.offset - access.tag());
  node->InsertInput(graph()->zone(), 1, offset);
  if (NeedsPoisoning(access.load_sensitivity) &&
      access.machine_type.representation() !=
          MachineRepresentation::kTaggedPointer) {
    NodeProperties::ChangeOp(node,
                             machine()->PoisonedLoad(access.machine_type));
  } else {
    NodeProperties::ChangeOp(node, machine()->Load(access.machine_type));
  }
  return Changed(node);
}

Reduction MemoryLowering::ReduceStoreElement(Node* node) {
  DCHECK_EQ(IrOpcode::kStoreElement, node->opcode());
  ElementAccess const& access = ElementAccessOf(node->op());
  Node* index = node->InputAt(1);
  WriteBarrierKind write_barrier_kind = access.write_barrier_kind;
  node->ReplaceInput(1, ComputeIndex(access, index));
  NodeProperties::ChangeOp(
      node, machine()->Store(StoreRepresentation(
                access.machine_type.representation(), write_barrier_kind)));
  return Changed(node);
}

Reduction MemoryLowering::ReduceStoreField(Node* node) {
  DCHECK_EQ(IrOpcode::kStoreField, node->opcode());
  FieldAccess const& access = FieldAccessOf(node->op());
  WriteBarrierKind write_barrier_kind = access.write_barrier_kind;
  Node* offset = jsgraph()->IntPtrConstant(access.offset - access.tag());
  node->InsertInput(graph()->zone(), 1, offset);
  NodeProperties::ChangeOp(
      node, machine()->Store(StoreRepresentation(
                access.machine_type.representation(), write_barrier_kind)));
  return Changed(node);
}

bool MemoryLowering::NeedsPoisoning(LoadSensitivity load_sensitivity) const {
  // Safe loads do not need poisoning.
  if (load_sensitivity == LoadSensitivity::kSafe) return false;

  switch (poisoning_level_) {
    case PoisoningMitigationLevel::kDontPoison:
      return false;
    case PoisoningMitigationLevel::kPoisonAll:
      return true;
    case PoisoningMitigationLevel::kPoisonCriticalOnly:
      return load_sensitivity == LoadSensitivity::kCritical;
  }
  UNREACHABLE();
}

Node* MemoryLowering::ComputeIndex(ElementAccess const& access, Node* key) {
  Node* index;
  if (machine()->Is64()) {
    // On 64-bit platforms, we need to feed a Word64 index to the Load and
    // Store operators. Since LoadElement or StoreElement don't do any bounds
    // checking themselves, we can be sure that the {key} was already checked
    // and is in valid range, so we can do the further address computation on
    // Word64 below, which ideally allows us to fuse the address computation
    // with the actual memory access operation on Intel platforms.
    index = graph()->NewNode(machine()->ChangeUint32ToUint64(), key);
  } else {
    index = key;
  }
  int const element_size_shift =
      ElementSizeLog2Of(access.machine_type.representation());
  if (element_size_shift) {
    index = graph()->NewNode(machine()->WordShl(), index,
                             jsgraph()->IntPtrConstant(element_size_shift));
  }
  int const fixed_offset = access.header_size - access.tag();
  if (fixed_offset) {
    index = graph()->NewNode(machine()->IntAdd(), index,
                             jsgraph()->IntPtrConstant(fixed_offset));
  }
  return index;
}

Graph* MemoryLowering::graph() const { return jsgraph()->graph(); }

CommonOperatorBuilder* MemoryLowering::common() const {
  return jsgraph()->common();
}

MachineOperatorBuilder* MemoryLowering::machine() const {
  return jsgraph()->machine();
}

Isolate* MemoryLowering::isolate() const { return jsgraph()->isolate(); }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
