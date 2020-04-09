// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/basic-block-instrumentor.h"

#include <sstream>

#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/schedule.h"
#include "src/heap/heap-inl.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

// Find the first place to insert new nodes in a block that's already been
// scheduled that won't upset the register allocator.
static NodeVector::iterator FindInsertionPoint(BasicBlock* block) {
  NodeVector::iterator i = block->begin();
  for (; i != block->end(); ++i) {
    const Operator* op = (*i)->op();
    if (OperatorProperties::IsBasicBlockBegin(op)) continue;
    switch (op->opcode()) {
      case IrOpcode::kParameter:
      case IrOpcode::kPhi:
      case IrOpcode::kEffectPhi:
        continue;
    }
    break;
  }
  return i;
}

BasicBlockProfiler::Data* BasicBlockInstrumentor::Instrument(
    OptimizedCompilationInfo* info, Graph* graph, Schedule* schedule,
    Isolate* isolate) {
  // Basic block profiling disables concurrent compilation, so handle deref is
  // fine.
  AllowHandleDereference allow_handle_dereference;
  // Skip the exit block in profiles, since the register allocator can't handle
  // it and entry into it means falling off the end of the function anyway.
  size_t n_blocks = static_cast<size_t>(schedule->RpoBlockCount()) - 1;
  BasicBlockProfiler::Data* data = BasicBlockProfiler::Get()->NewData(n_blocks);
  // Set the function name.
  data->SetFunctionName(info->GetDebugName());
  // Capture the schedule string before instrumentation.
  {
    // std::ostringstream os;
    // os << *schedule;
    // data->SetSchedule(&os);
  }
  // Add the increment instructions to the start of every block.
  CommonOperatorBuilder common(graph->zone());
  // TODO(seth.brenith): should be based on kSmiShiftSize, use intptr not int32.
  // Or use byte arrays rather than FixedArray.
  Node* one = graph->NewNode(common.Int32Constant(2));
  MachineOperatorBuilder machine(graph->zone());
  BasicBlockVector* blocks = schedule->rpo_order();
  size_t block_number = 0;
  Handle<FixedArray> branch_counters(isolate->heap()->branch_counters(),
                                     isolate);
  // branch_counters.objects[0] contains the next unassigned index.
  int start_index = Smi::ToInt(branch_counters->get(0));
  int max_id = -1;
  for (BasicBlockVector::iterator it = blocks->begin(); block_number < n_blocks;
       ++it, ++block_number) {
    BasicBlock* block = (*it);
    if (block->id().ToInt() > max_id) max_id = block->id().ToInt();
    Node* offset = graph->NewNode(common.Int32Constant(
        branch_counters->OffsetOfElementAt(start_index + block->id().ToInt()) -
        kHeapObjectTag));
    data->SetBlockRpoNumber(block_number, block->rpo_number());
    // TODO(dcarney): wire effect and control deps for load and store.
    // Construct increment operation.
    Node* base = graph->NewNode(common.HeapConstant(branch_counters));
    Node* load = graph->NewNode(machine.Load(MachineType::Uint32()), base,
                                offset, graph->start(), graph->start());
    Node* inc = graph->NewNode(machine.Int32Add(), load, one);
    Node* store =
        graph->NewNode(machine.Store(StoreRepresentation(
                           MachineRepresentation::kWord32, kNoWriteBarrier)),
                       base, offset, inc, graph->start(), graph->start());
    // Insert the new nodes.
    static const int kArraySize = 6;
    Node* to_insert[kArraySize] = {one, offset, base, load, inc, store};
    int insertion_start = block_number == 0 ? 0 : 1;
    NodeVector::iterator insertion_point = FindInsertionPoint(block);
    block->InsertNodes(insertion_point, &to_insert[insertion_start],
                       &to_insert[kArraySize]);
    // Tell the scheduler about the new nodes.
    for (int i = insertion_start; i < kArraySize; ++i) {
      schedule->SetBlockForNode(block, to_insert[i]);
    }
  }
  return data;
}

void BasicBlockInstrumentor::UpdateNextIdCounter(Schedule* schedule,
                                                 Isolate* isolate) {
  // Basic block profiling disables concurrent compilation, so handle deref is
  // fine.
  AllowHandleDereference allow_handle_dereference;
  // Skip the exit block in profiles, since the register allocator can't handle
  // it and entry into it means falling off the end of the function anyway.
  size_t n_blocks = static_cast<size_t>(schedule->RpoBlockCount()) - 1;
  BasicBlockVector* blocks = schedule->rpo_order();
  size_t block_number = 0;
  Handle<FixedArray> branch_counters(isolate->heap()->branch_counters(),
                                     isolate);
  // branch_counters.objects[0] contains the next unassigned index.
  int start_index = Smi::ToInt(branch_counters->get(0));
  int max_id = -1;
  for (BasicBlockVector::iterator it = blocks->begin(); block_number < n_blocks;
       ++it, ++block_number) {
    BasicBlock* block = (*it);
    if (block->id().ToInt() > max_id) max_id = block->id().ToInt();
  }
  // Update the next unassigned index based on how many indices this function
  // used.
  int next_unassigned_index = start_index + max_id + 1;
  branch_counters->set(0, Smi::FromInt(next_unassigned_index));
  CHECK_WITH_MSG(
      next_unassigned_index <= branch_counters->length(),
      "We are trying to instrument more basic blocks than the available space "
      "for profiling. Search for num_basic_blocks in setup-heap-internal.cc.");
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
