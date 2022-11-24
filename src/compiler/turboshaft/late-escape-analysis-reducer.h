// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LATE_ESCAPE_ANALYSIS_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_LATE_ESCAPE_ANALYSIS_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8::internal::compiler::turboshaft {

// LateEscapeAnalysis removes allocation that have no uses besides the stores
// initializing the object.

class LateEscapeAnalysisAnalyzer {
 public:
  LateEscapeAnalysisAnalyzer(Graph& graph, Zone* zone)
      : graph_(graph),
        phase_zone_(zone),
        alloc_uses_(zone),
        allocs_(zone),
        operations_to_skip_(zone) {}

  void Run();

  bool ShouldSkipOperation(OpIndex index) {
    return operations_to_skip_.find(index) != operations_to_skip_.end();
  }

 private:
  void RecordAllocateUse(OpIndex source, OpIndex dst) {
    ZoneVector<OpIndex>* uses = nullptr;
    if (alloc_uses_.find(source) == alloc_uses_.end()) {
      uses = phase_zone_->New<ZoneVector<OpIndex>>();
      alloc_uses_[source] = uses;
    } else {
      uses = alloc_uses_[source];
    }
    uses->push_back(dst);
  }

  void CollectUsesAndAllocations();
  void FindRemovableAllocations();
  bool AllocationIsEscaping(OpIndex alloc);
  bool MakeInputEscape(OpIndex alloc, OpIndex using_op_idx);
  void MarkToRemove(OpIndex alloc);

  Graph& graph_;
  Zone* phase_zone_;

  // {alloc_uses_} records all the uses of each AllocateOp.
  ZoneUnorderedMap<OpIndex, ZoneVector<OpIndex>*> alloc_uses_;
  // {allocs_} is filled with all of the AllocateOp of the graph, and then
  // iterated upon to determine which allocations can be removed and which
  // cannot.
  ZoneVector<OpIndex> allocs_;
  // {operations_to_skip_} contains all of the AllocateOp and StoreOp that can
  // be removed.
  ZoneUnorderedSet<OpIndex> operations_to_skip_;
};

template <class Next>
class LateEscapeAnalysisReducer : Next {
 public:
  using Next::Asm;

  template <class... Args>
  explicit LateEscapeAnalysisReducer(const std::tuple<Args...>& args)
      : Next(args), analyzer_(Asm().input_graph(), Asm().phase_zone()) {}

  void Analyze() {
    analyzer_.Run();
    Next::Analyze();
  }

  OpIndex ReduceStore(OpIndex base, OpIndex index, OpIndex value,
                      StoreOp::Kind kind, MemoryRepresentation stored_rep,
                      WriteBarrierKind write_barrier, int32_t offset,
                      uint8_t element_scale) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceStore(base, index, value, kind, stored_rep,
                               write_barrier, offset, element_scale);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    if (analyzer_.ShouldSkipOperation(this->current_operation_origin_)) {
      return OpIndex::Invalid();
    }

    goto no_change;
  }

  OpIndex ReduceAllocate(OpIndex size, Type type, AllocationType allocation,
                         AllowLargeObjects allow_large_objects) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceAllocateRaw(size, type, allocation,
                                     allow_large_objects);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    if (analyzer_.ShouldSkipOperation(this->current_operation_origin_)) {
      return OpIndex::Invalid();
    }

    goto no_change;
  }

 private:
  LateEscapeAnalysisAnalyzer analyzer_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LATE_ESCAPE_ANALYSIS_REDUCER_H_
