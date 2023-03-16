// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/store-fusion-optimizer.h"

#include "src/common/ptr-compr-inl.h"
#include "src/compiler/graph.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

bool StoreFusionOptimizer::GetRootIndexIfUsable(Handle<HeapObject> val,
                                                RootIndex& root_index) {
  if (!isolate_) return false;
  const RootsTable& roots_table = isolate_->roots_table();
  if (!val.is_null() && roots_table.IsRootHandle(val, &root_index)) {
    if (RootsTable::IsReadOnly(root_index) &&
        (V8_STATIC_ROOTS_BOOL || !isolate_->bootstrapper())) {
      return true;
    }
  }
  return false;
}

template <typename It>
bool StoreFusionOptimizer::TryMerge(Node* node1, Node* node2, BasicBlock* block,
                                    It& pos) {
  if (node1->opcode() != IrOpcode::kStore ||
      node2->opcode() != IrOpcode::kStore) {
    return false;
  }

  Node* base1 = node1->InputAt(0);
  Node* base2 = node2->InputAt(0);
  if (base1 != base2) return false;

  auto rep1 = StoreRepresentationOf(node1->op());
  auto rep2 = StoreRepresentationOf(node2->op());
  if (rep1.write_barrier_kind() != WriteBarrierKind::kNoWriteBarrier ||
      rep2.write_barrier_kind() != WriteBarrierKind::kNoWriteBarrier ||
      ElementSizeLog2Of(rep1.representation()) != 2 ||
      ElementSizeLog2Of(rep2.representation()) != 2) {
    return false;
  }

  Node* index1 = node1->InputAt(1);
  Node* index2 = node2->InputAt(1);

  if (index1->opcode() != IrOpcode::kInt64Constant ||
      index2->opcode() != IrOpcode::kInt64Constant) {
    return false;
  }
  int idx1 = static_cast<int32_t>(OpParameter<int64_t>(index1->op()));
  int idx2 = static_cast<int32_t>(OpParameter<int64_t>(index2->op()));

  if (idx1 - 4 != idx2 && idx1 + 4 != idx2) {
    return false;
  }

  Node* value1 = node1->InputAt(2);
  Node* value2 = node2->InputAt(2);

  if (value1->opcode() == IrOpcode::kTruncateInt64ToInt32 ||
      value1->opcode() == IrOpcode::kChangeUint32ToUint64) {
    value1 = value1->InputAt(0);
  }
  if (value2->opcode() == IrOpcode::kTruncateInt64ToInt32 ||
      value2->opcode() == IrOpcode::kChangeUint32ToUint64) {
    value2 = value2->InputAt(0);
  }

  ReadOnlyRoots roots(isolate_);
  uint32_t const1;
  if (value1->opcode() == IrOpcode::kInt32Constant) {
    const1 = static_cast<uint32_t>(OpParameter<int32_t>(value1->op()));
  } else if (value1->opcode() == IrOpcode::kInt64Constant) {
    const1 = static_cast<uint32_t>(OpParameter<int64_t>(value1->op()));
  } else if (value1->opcode() == IrOpcode::kHeapConstant) {
    RootIndex root_index;
    HeapObjectMatcher m(value1);
    if (m.HasResolvedValue() &&
        GetRootIndexIfUsable(m.ResolvedValue(), root_index)) {
      const1 =
          V8HeapCompressionScheme::CompressObject(roots.address_at(root_index));
    } else {
      return false;
    }
  } else if (value1->opcode() == IrOpcode::kCompressedHeapConstant) {
    RootIndex root_index;
    CompressedHeapObjectMatcher m(value1);
    if (m.HasResolvedValue() &&
        GetRootIndexIfUsable(m.ResolvedValue(), root_index)) {
      const1 =
          V8HeapCompressionScheme::CompressObject(roots.address_at(root_index));
    } else {
      return false;
    }
  } else {
    return false;
  }

  uint32_t const2;
  if (value2->opcode() == IrOpcode::kInt32Constant) {
    const2 = static_cast<uint32_t>(OpParameter<int32_t>(value2->op()));
  } else if (value2->opcode() == IrOpcode::kInt64Constant) {
    const2 = static_cast<uint32_t>(OpParameter<int64_t>(value2->op()));
  } else if (value2->opcode() == IrOpcode::kHeapConstant) {
    RootIndex root_index;
    HeapObjectMatcher m(value2);
    if (m.HasResolvedValue() &&
        GetRootIndexIfUsable(m.ResolvedValue(), root_index)) {
      const2 =
          V8HeapCompressionScheme::CompressObject(roots.address_at(root_index));
    } else {
      return false;
    }
  } else if (value2->opcode() == IrOpcode::kCompressedHeapConstant) {
    RootIndex root_index;
    CompressedHeapObjectMatcher m(value2);
    if (m.HasResolvedValue() &&
        GetRootIndexIfUsable(m.ResolvedValue(), root_index)) {
      const2 =
          V8HeapCompressionScheme::CompressObject(roots.address_at(root_index));
    } else {
      return false;
    }
  } else {
    return false;
  }

  Node* index = index1;
  uint64_t combined;
  if (idx1 < idx2) {
    combined = (static_cast<uint64_t>(const2) << 32) + const1;
  } else {
    index = index2;
    combined = (static_cast<uint64_t>(const1) << 32) + const2;
  }

  Node* combined_value =
      graph_->NewNode(common_->Int64Constant(combined), 0, {}, false);
  Node* inputs[5] = {base1, index, combined_value, node1->InputAt(3),
                     node2->InputAt(4)};
  Node* replace =
      graph_->NewNode(machine_->Store({MachineRepresentation::kWord64,
                                       WriteBarrierKind::kNoWriteBarrier}),
                      5, inputs, false);
  node1->ReplaceUses(replace);
  node2->ReplaceUses(replace);
  *pos = combined_value;
  pos++;
  *pos = replace;
  pos++;
  schedule_->LateAdd(combined_value, block);
  schedule_->LateAdd(replace, block);
  node1->NullAllInputs();
  node2->NullAllInputs();
  return true;
}

void StoreFusionOptimizer::Fuse() {
  // if ((true)) return;

  USE(zone_);
  BasicBlockVector* blocks = schedule_->rpo_order();
  for (auto block = blocks->begin(); block != blocks->end(); ++block) {
    auto nodes = (*block)->nodes();
    auto pos = nodes->begin();
    while (pos != nodes->end()) {
      auto node = *pos;
      auto next = pos + 1;
      if (next == nodes->end()) break;
      if (!TryMerge(node, *next, *block, pos)) {
        ++pos;
      }
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
