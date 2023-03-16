// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_STORE_FUSION_OPTIMIZER_H_
#define V8_COMPILER_STORE_FUSION_OPTIMIZER_H_

#include "src/compiler/common-operator.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/schedule.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declare.
class Graph;

class V8_EXPORT_PRIVATE StoreFusionOptimizer final {
 public:
  StoreFusionOptimizer(Zone* zone, Isolate* isolate, Graph* graph,
                       Schedule* schedule, CommonOperatorBuilder* common,
                       MachineOperatorBuilder* machine)
      : zone_(zone),
        isolate_(isolate),
        graph_(graph),
        schedule_(schedule),
        common_(common),
        machine_(machine) {}
  ~StoreFusionOptimizer() = default;
  StoreFusionOptimizer(const StoreFusionOptimizer&) = delete;
  StoreFusionOptimizer& operator=(const StoreFusionOptimizer&) = delete;

  void Fuse();

 private:
  template <typename It>
  bool TryMerge(Node* node1, Node* node2, BasicBlock* block, It& pos);
  bool GetRootIndexIfUsable(Handle<HeapObject> val, RootIndex& root_index);

  Zone* zone_;
  Isolate* isolate_;
  Graph* graph_;
  Schedule* schedule_;
  CommonOperatorBuilder* const common_;
  MachineOperatorBuilder* const machine_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_STORE_FUSION_OPTIMIZER_H_
