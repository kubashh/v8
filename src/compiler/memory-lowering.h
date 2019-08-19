// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_MEMORY_REDUCER_H_
#define V8_COMPILER_MEMORY_REDUCER_H_

#include "src/compiler/graph-assembler.h"
#include "src/compiler/graph-reducer.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
class Graph;

// Lowers Memory nodes.
class MemoryLowering final : public Reducer {
public:
  MemoryLowering(JSGraph* jsgraph, Zone* zone,
                 PoisoningMitigationLevel poisoning_level);
  ~MemoryLowering() {};

  const char* reducer_name() const override { return "MemoryLowering"; }

  Reduction Reduce(Node* node) override;

 private:
  Reduction ReduceAllocateRaw(Node*);
  Reduction ReduceLoadElement(Node*);
  Reduction ReduceLoadField(Node*);
  Reduction ReduceStoreElement(Node*);
  Reduction ReduceStoreField(Node*);

  bool NeedsPoisoning(LoadSensitivity load_sensitivity) const;

  Node* ComputeIndex(ElementAccess const&, Node*);

  GraphAssembler* gasm() { return &graph_assembler_; }

  Isolate* isolate() const;
  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  CommonOperatorBuilder* common() const;
  MachineOperatorBuilder* machine() const;
  Zone* zone() const { return zone_; }

  SetOncePointer<const Operator> allocate_operator_;
  JSGraph* const jsgraph_;
  Zone* const zone_;
  GraphAssembler graph_assembler_;
  PoisoningMitigationLevel poisoning_level_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_MEMORY_REDUCER_H_
