// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_REVECTORIZER_H_
#define V8_COMPILER_REVECTORIZER_H_

#include <vector>

#include "src/base/small-vector.h"
#include "src/compiler/graph.h"
#include "src/compiler/linear-scheduler.h"
#include "src/compiler/machine-graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/schedule.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

struct V8_EXPORT_PRIVATE StoreOffsetComparer {
  bool operator()(const Node* lhs, const Node* rhs) const;
};

using StoreNodeSet = ZoneSet<Node*, StoreOffsetComparer>;

struct PackNode {
  std::vector<Node*> Nodes;
  Node* RevectorizedNode = nullptr;
  std::vector<PackNode*> Operands;

  bool IsSame(const std::vector<Node*>& node_group) const {
    if (node_group.size() == Nodes.size()) {
      return std::equal(node_group.begin(), node_group.end(), Nodes.begin());
    }
    return false;
  }

  // returns the OpIdx operand of this PackNode.
  PackNode* GetOperand(unsigned OpIdx) {
    DCHECK_LT(OpIdx, Operands.size());
    return Operands[OpIdx];
  }

  // returns the number of operands.
  std::vector<PackNode*>::size_type GetNumOperands() const {
    return Operands.size();
  }

  void SetOperand(unsigned OpIdx, PackNode* Op) {
    if (Operands.size() < OpIdx + 1) Operands.resize(OpIdx + 1);
    Operands[OpIdx] = Op;
  }
};

class SLPTree : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  explicit SLPTree(Zone* zone, Graph* graph)
      : zone_(zone), graph_(graph), root_(nullptr) {
    scheduler_ = new LinearScheduler(zone, graph);
    on_stack_ = {};
    stack_ = {};
  }

  PackNode* BuildTree(const std::vector<Node*>& Roots);
  void DeleteTree();

  PackNode* GetPackNode(Node* node);

  enum { RecursionMaxDepth = 1000, SLPCostThreshold = 2000 };

  void Print(const char* info);

  template <typename FunctionType>
  void ForEach(FunctionType callback);

  Node* GetEarlySchedulePosition(Node* node) {
    return scheduler_->GetEarlySchedulePosition(node);
  }

 private:
  friend class LinearScheduler;

  // This is the recursive part of buildTree.
  PackNode* BuildTreeRec(const std::vector<Node*>& node_group, unsigned Depth);
  PackNode* NewPackNode(const std::vector<Node*>& node_group);

  PackNode* BuildPackNode(const std::vector<Node*>& node_group, int start_index,
                          int count, unsigned Depth);

  bool IsSupported(const std::vector<Node*>& node_group);

  Graph* graph() const { return graph_; }

  // Node stack operations.
  void PopStack();
  void PushStack(const std::vector<Node*>& node_group);
  void ClearStack();
  bool OnStack(Node* node);
  bool OnStack(const std::vector<Node*>& node_group);
  bool StackTopIsPhi();

  bool IsLeaf(const std::vector<Node*>& node_group);

  bool HasInternalDependency(const std::vector<Node*>& node_group);
  bool SameBasicBlock(Node* node0, Node* node1) {
    return scheduler_->SameBasicBlock(node0, node1);
  }

  Zone* const zone_;
  // Maps a specific scalar to its tree entry.
  std::unordered_map<Node*, PackNode*> node_to_packnode_;
  Graph* const graph_;
  PackNode* root_;
  LinearScheduler* scheduler_;
  std::unordered_set<Node*> on_stack_;
  std::stack<std::vector<Node*>> stack_;
};

class V8_EXPORT_PRIVATE Revectorizer final
    : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  Revectorizer(Zone* zone, Graph* graph, MachineGraph* mcgraph)
      : zone_(zone),
        graph_(graph),
        mcgraph_(mcgraph),
        group_of_stores_(zone),
        has_avx_(false),
        has_avx2_(false) {
    DetectCPUFeatures();
    slp_tree_ = zone_->New<SLPTree>(zone, graph);
  }

  void DetectCPUFeatures();
  bool TryRevectorize(const char* name);

 private:
  void CollectSeeds();

  bool ReduceStoreChains(ZoneMap<Node*, StoreNodeSet>* store_chains);
  bool ReduceStoreChain(const std::vector<Node*>& Stores);

  void PrintStores(ZoneMap<Node*, StoreNodeSet>* store_chains);
  Zone* zone() const { return zone_; }
  Graph* graph() const { return graph_; }
  MachineGraph* mcgraph() const { return mcgraph_; }

  PackNode* GetPackNode(Node* node) const {
    return slp_tree_->GetPackNode(node);
  }

  bool DecideVectorize();

  void SetEffectInput(PackNode* pnode, int index, Node*& nput);
  void SetMemoryOpInputs(base::SmallVector<Node*, 2>& inputs, PackNode* pnode,
                         int index);
  Node* VectorizeTree(PackNode* pnode);

  Zone* const zone_;
  Graph* const graph_;
  MachineGraph* const mcgraph_;
  ZoneMap<Node*, ZoneMap<Node*, StoreNodeSet>*> group_of_stores_;
  SLPTree* slp_tree_;

  bool has_avx_;
  bool has_avx2_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_REVECTORIZER_H_
