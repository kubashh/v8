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

class PackNode final : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  PackNode(Zone* zone, const std::vector<Node*>& node_group)
      : nodes_(node_group.cbegin(), node_group.cend(), zone),
        operands_(zone),
        revectorized_node_(nullptr) {}
  const ZoneVector<Node*>& Nodes() const { return nodes_; }
  bool IsSame(const std::vector<Node*>& node_group) const {
    if (node_group.size() == nodes_.size()) {
      return std::equal(node_group.begin(), node_group.end(), nodes_.begin());
    }
    return false;
  }
  Node* RevectorizedNode() { return revectorized_node_; }
  void SetRevectorizedNode(Node* node) { revectorized_node_ = node; }
  // returns the index operand of this PackNode.
  PackNode* GetOperand(unsigned index) {
    DCHECK_LT(index, operands_.size());
    return operands_[index];
  }

  // returns the number of operands.
  std::vector<PackNode*>::size_type GetNumOperands() const {
    return operands_.size();
  }

  void SetOperand(unsigned index, PackNode* pnode) {
    if (operands_.size() < index + 1) operands_.resize(index + 1);
    operands_[index] = pnode;
  }

  void Print() const;

 private:
  ZoneVector<Node*> nodes_;
  ZoneVector<PackNode*> operands_;
  Node* revectorized_node_;
};

class SLPTree : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  explicit SLPTree(Zone* zone, Graph* graph)
      : zone_(zone),
        graph_(graph),
        root_(nullptr),
        on_stack_(zone),
        stack_(zone),
        node_to_packnode_(zone) {
    scheduler_ = zone->New<LinearScheduler>(zone, graph);
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
  Graph* const graph_;
  PackNode* root_;
  LinearScheduler* scheduler_;
  ZoneSet<Node*> on_stack_;
  ZoneStack<std::vector<Node*>> stack_;
  // Maps a specific node to PackNode.
  ZoneUnorderedMap<Node*, PackNode*> node_to_packnode_;
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
