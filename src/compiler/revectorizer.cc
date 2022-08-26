// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/revectorizer.h"

#include "src/base/cpu.h"
#include "src/base/logging.h"
#include "src/compiler/all-nodes.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/verifier.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE(...)                     \
  do {                                 \
    if (FLAG_trace_wasm_revectorize) { \
      PrintF("Revec: ");               \
      PrintF(__VA_ARGS__);             \
    }                                  \
  } while (false)

///////////// static method

static bool IsLoad(Node* node) {
  if (node->opcode() == IrOpcode::kProtectedLoad ||
      node->opcode() == IrOpcode::kLoad ||
      node->opcode() == IrOpcode::kLoadTransform) {
    return true;
  }
  return false;
}

static int64_t GetConstantValue(Node* node) {
  int64_t value = -1;
  if (node->opcode() == IrOpcode::kInt64Constant) {
    value = OpParameter<int64_t>(node->op());
  }
  return value;
}

static int64_t GetMemoryOffsetValue(const Node* node) {
  DCHECK(node->opcode() == IrOpcode::kProtectedLoad ||
         node->opcode() == IrOpcode::kStore ||
         node->opcode() == IrOpcode::kProtectedStore);

  Node* offset = node->InputAt(0);
  if (offset->opcode() == IrOpcode::kLoadFromObject ||
      offset->opcode() == IrOpcode::kLoad) {
    return 0;
  }

  int64_t offval = -1;
  if (offset->opcode() == IrOpcode::kInt64Add) {
    if (NodeProperties::IsConstant(offset->InputAt(0))) {
      offval = GetConstantValue(offset->InputAt(0));
    } else if (NodeProperties::IsConstant(offset->InputAt(1))) {
      offval = GetConstantValue(offset->InputAt(1));
    }
  }
  return offval;
}

static Node* GetNodeAddr(Node* node) {
  Node* addr = node->InputAt(1);
  if (addr->opcode() == IrOpcode::kChangeUint32ToUint64) {
    addr = addr->InputAt(0);
  }

  return addr;
}

static bool IsContinuousAccess(const std::vector<Node*>& node_group) {
  DCHECK_GT(node_group.size(), 0);
  int64_t offpre = GetMemoryOffsetValue(node_group[0]);
  for (size_t i = 1; i < node_group.size(); ++i) {
    int64_t offcurr = GetMemoryOffsetValue(node_group[i]);
    int64_t diff = offcurr - offpre;
    if (diff != 16) {
      TRACE("Non-continuous store!");
      return false;
    }
    offpre = offcurr;
  }
  return true;
}

// returns True if all of the values in node_group are constants.
static bool AllConstant(const std::vector<Node*>& node_group) {
  for (Node* node : node_group)
    if (!NodeProperties::IsConstant(node)) return false;
  return true;
}

static bool AllSameAddr(const std::vector<Node*>& nodes) {
  Node* addr = GetNodeAddr(nodes[0]);
  for (size_t i = 1, e = nodes.size(); i < e; i++) {
    if (GetNodeAddr(nodes[i]) != addr) {
      TRACE("Diff addr #%d,#%d!\n", addr->id(), GetNodeAddr(nodes[i])->id());
      return false;
    }
  }

  return true;
}

// returns True if all of the values in node_group are identical.
static bool IsSplat(const std::vector<Node*>& node_group) {
  for (std::vector<Node*>::size_type i = 1, e = node_group.size(); i < e; ++i)
    if (node_group[i] != node_group[0]) return false;
  return true;
}

// returns true if all of the values in node_group have the same type or false
// otherwise.
static bool AllSameType(const std::vector<Node*>& node_group) {
  IrOpcode::Value op = node_group[0]->opcode();
  for (std::vector<Node*>::size_type i = 1, e = node_group.size(); i < e; i++)
    if (node_group[i]->opcode() != op) return false;

  return true;
}

// sort by offset
bool StoreOffsetComparer::operator()(const Node* lhs, const Node* rhs) const {
  return GetMemoryOffsetValue(lhs) < GetMemoryOffsetValue(rhs);
}

//////////////////////////////////

bool SLPTree::IsSupported(const std::vector<Node*>& node_group) {
  if (!SameBasicBlock(node_group[0], node_group[1])) {
    TRACE("%s(#%d, #%d) not in same BB!\n", node_group[0]->op()->mnemonic(),
          node_group[0]->id(), node_group[1]->id());
    return false;
  }
  if (!AllSameType(node_group)) {
    TRACE("%s(#%d, #%d) have different type!\n",
          node_group[0]->op()->mnemonic(), node_group[0]->id(),
          node_group[1]->id());
    return false;
  }
  // TODO(jiepan): add support for Constant
  if (AllConstant(node_group)) {
    TRACE("%s(#%d, #%d) are constantant, not supported yet!\n",
          node_group[0]->op()->mnemonic(), node_group[0]->id(),
          node_group[1]->id());
    return false;
  }

  IrOpcode::Value op = node_group[0]->opcode();
  if (!NodeProperties::IsSimd128(node_group[0]) && (op != IrOpcode::kStore) &&
      (op != IrOpcode::kProtectedStore) && (op != IrOpcode::kLoad) &&
      (op != IrOpcode::kProtectedLoad) && (op != IrOpcode::kPhi) &&
      (op != IrOpcode::kLoopExitValue) && (op != IrOpcode::kExtractF128)) {
    return false;
  }
  return true;
}

// Create a new PackNode.
PackNode* SLPTree::NewPackNode(const std::vector<Node*>& node_group) {
  TRACE("PackNode %s(#%d:, #%d)\n", node_group[0]->op()->mnemonic(),
        node_group[0]->id(), node_group[1]->id());
  PackNode* Last = new PackNode();
  Last->Nodes.insert(Last->Nodes.begin(), node_group.begin(), node_group.end());
  for (Node* node : node_group) {
    node_to_packnode_[node] = Last;
  }
  return Last;
}

PackNode* SLPTree::BuildPackNode(const std::vector<Node*>& node_group,
                                 int start_index, int count,
                                 unsigned recursion_depth) {
  PackNode* pnode = NewPackNode(node_group);
  for (int i = start_index, e = start_index + count; i < e; ++i) {
    std::vector<Node*> Operands;
    // Prepare the operand vector.
    for (size_t j = 0; j < node_group.size(); j++) {
      Node* node = node_group[j];
      Operands.push_back(NodeProperties::GetValueInput(node, i));
    }

    PackNode* child = BuildTreeRec(Operands, recursion_depth + 1);
    if (child)
      pnode->SetOperand(i, child);
    else
      return nullptr;
  }

  return pnode;
}

PackNode* SLPTree::GetPackNode(Node* node) {
  auto I = node_to_packnode_.find(node);
  if (I != node_to_packnode_.end()) {
    return I->second;
  }
  return nullptr;
}

void SLPTree::PushStack(const std::vector<Node*>& node_group) {
  TRACE("Stack Push (%d %s, %d %s)\n", node_group[0]->id(),
        node_group[0]->op()->mnemonic(), node_group[1]->id(),
        node_group[1]->op()->mnemonic());
  for (auto node : node_group) {
    on_stack_.insert(node);
  }
  stack_.push({node_group});
}

void SLPTree::PopStack() {
  const std::vector<Node*>& node_group = stack_.top();
  TRACE("Stack Pop (%d %s, %d %s)\n", node_group[0]->id(),
        node_group[0]->op()->mnemonic(), node_group[1]->id(),
        node_group[1]->op()->mnemonic());
  for (auto node : node_group) {
    on_stack_.erase(node);
  }
  stack_.pop();
}

bool SLPTree::OnStack(Node* node) {
  return on_stack_.find(node) != on_stack_.end();
}

bool SLPTree::OnStack(const std::vector<Node*>& node_group) {
  for (auto node : node_group) {
    if (OnStack(node)) return true;
  }
  return false;
}

bool SLPTree::StackTopIsPhi() {
  const std::vector<Node*>& node_group = stack_.top();
  return NodeProperties::IsPhi(node_group[0]);
}

void SLPTree::ClearStack() {
  stack_ = {};
  on_stack_ = {};
}

bool SLPTree::IsLeaf(const std::vector<Node*>& node_group) {
  for (auto node : node_group) {
    if (!IsLoad(node)) return false;
  }
  return true;
}

bool SLPTree::HasInternalDependency(const std::vector<Node*>& node_group) {
  Node* node0 = node_group[0];
  Node* node1 = node_group[1];
  std::deque<Node*> queue;
  std::unordered_set<Node*> visited;
  TRACE("Enter HasInternalDependency (%d %s, %d %s)\n", node0->id(),
        node0->op()->mnemonic(), node1->id(), node1->op()->mnemonic());
  DCHECK(IsLeaf(node_group));

  for (int i = 0; i < NodeProperties::FirstControlIndex(node0); ++i) {
    Node* input = node0->InputAt(i);
    if (input != node1) {
      queue.push_back(input);
    }
  }
  for (int i = 0; i < NodeProperties::FirstControlIndex(node1); ++i) {
    Node* input = node1->InputAt(i);
    if (input != node0) {
      queue.push_back(input);
    }
  }

  while (!queue.empty()) {
    Node* visit = queue.front();
    queue.pop_front();
    TRACE("HasInternalDependency visit (%d %s)\n", visit->id(),
          visit->op()->mnemonic());
    if (visited.find(visit) != visited.end()) {
      continue;
    } else {
      visited.insert(visit);
    }

    if (OnStack(visit)) {
      TRACE("Has internal dependency because (%d %s) on stack\n", visit->id(),
            visit->op()->mnemonic());
      return true;
    }

    if (SameBasicBlock(visit, node0)) {
      for (int i = 0; i < NodeProperties::FirstControlIndex(visit); ++i) {
        queue.push_back(visit->InputAt(i));
      }
    }
  }
  return false;
}

PackNode* SLPTree::BuildTree(const std::vector<Node*>& Roots) {
  TRACE("Enter %s\n", __func__);

  DeleteTree();

  root_ = BuildTreeRec(Roots, 0);
  return root_;
}

PackNode* SLPTree::BuildTreeRec(const std::vector<Node*>& node_group,
                                unsigned recursion_depth) {
  TRACE("Enter %s\n", __func__);
  DCHECK_EQ(node_group.size(), 2);

  Node* node0 = node_group[0];
  Node* node1 = node_group[1];

  if (recursion_depth == RecursionMaxDepth) {
    TRACE("Failed due to max recursion depth!\n");
    return nullptr;
  }

  if (OnStack(node_group)) {
    if (!StackTopIsPhi()) {
      TRACE("Failed due to (%d %s, %d %s) on stack!\n", node0->id(),
            node0->op()->mnemonic(), node1->id(), node1->op()->mnemonic());
      return nullptr;
    }
  }
  PushStack(node_group);

  if (!IsSupported(node_group)) {
    return nullptr;
  }

  DCHECK(AllConstant(node_group) || AllSameType(node_group));

  // Check if this is a duplicate of another entry.
  for (Node* node : node_group) {
    if (PackNode* p = GetPackNode(node)) {
      if (!p->IsSame(node_group)) {
        // TODO(jiepan): Gathering due to partial overlap
        TRACE("Failed due to partial overlap at #%d,%s!\n", node->id(),
              node->op()->mnemonic());
        return nullptr;
      }

      PopStack();
      TRACE("Perfect diamond merge at #%d,%s\n", node->id(),
            node->op()->mnemonic());
      return p;
    }
  }

  if (node0->opcode() == IrOpcode::kExtractF128) {
    Node* source = node0->InputAt(0);
    TRACE("Extract leaf node from #%d,%s!\n", source->id(),
          source->op()->mnemonic());
    // For 256 only, check whether they are from the same source
    if (node0->InputAt(0) == node1->InputAt(0) &&
        (node0->InputAt(0)->opcode() == IrOpcode::kLoadTransform
             ? node0 == node1
             : OpParameter<int32_t>(node0->op()) + 1 ==
                   OpParameter<int32_t>(node1->op()))) {
      TRACE("Added a pair of Extract.\n");
      PackNode* TE = NewPackNode(node_group);
      PopStack();
      return TE;
    }
    TRACE("Failed due to ExtractF128!\n");
    return nullptr;
  }

  if (node0->opcode() == IrOpcode::kProtectedLoad ||
      node0->opcode() == IrOpcode::kLoadTransform) {
    TRACE("Load leaf node\n");
    if (!AllSameAddr(node_group)) {
      TRACE("Failed due to different load addr!\n");
      return nullptr;
    }
    // sort loads by offset
    if (node0->opcode() == IrOpcode::kProtectedLoad) {
      std::vector<Node*> Sortednode_group(node_group.size());
      partial_sort_copy(begin(node_group), end(node_group),
                        begin(Sortednode_group), end(Sortednode_group),
                        StoreOffsetComparer());
      if (!IsContinuousAccess(Sortednode_group)) {
        TRACE("Failed due to non-continuous load!\n");
        return nullptr;
      }
    }

    if (node0->opcode() == IrOpcode::kLoadTransform) {
      if (!IsSplat(node_group)) {
        TRACE("LoadTransform failed due to IsSplat!\n");
        return nullptr;
      }
      LoadTransformParameters params = LoadTransformParametersOf(node0->op());
      // TODO(jiepan): Support more LoadTransformation types
      if (params.transformation != LoadTransformation::kS128Load32Splat &&
          params.transformation != LoadTransformation::kS128Load64Splat) {
        TRACE("LoadTransform failed due to unsupported type #%d!\n",
              node0->id());
        return nullptr;
      }
    }

    if (HasInternalDependency(node_group)) {
      TRACE("Failed due to dependency check\n");
      return nullptr;
    }
    PackNode* p = NewPackNode(node_group);
    PopStack();
    return p;
  }

  IrOpcode::Value op = node0->opcode();
  int value_in_count = node0->op()->ValueInputCount();
  switch (op) {
    case IrOpcode::kPhi: {
      TRACE("Added a vector of PHI nodes.\n");
      MachineRepresentation rep = PhiRepresentationOf(node0->op());
      if (rep != MachineRepresentation::kSimd128) {
        return nullptr;
      }

      PackNode* TE =
          BuildPackNode(node_group, 0, value_in_count, recursion_depth);
      PopStack();
      return TE;
    }
    case IrOpcode::kLoopExitValue: {
      MachineRepresentation rep = LoopExitValueRepresentationOf(node0->op());
      if (rep != MachineRepresentation::kSimd128) {
        return nullptr;
      }

      PackNode* TE =
          BuildPackNode(node_group, 0, value_in_count, recursion_depth);
      PopStack();
      return TE;
    }
    case IrOpcode::kF32x4Add:
    case IrOpcode::kF32x4Mul: {
      TRACE("Added a vector of un/bin/ter op.\n");
      PackNode* TE =
          BuildPackNode(node_group, 0, value_in_count, recursion_depth);
      PopStack();
      return TE;
    }

    case IrOpcode::kStore:
    case IrOpcode::kProtectedStore:
      // TODO(jiepan): UnalignedStore,
      {
        TRACE("Added a vector of stores.\n");
        if (!AllSameAddr(node_group)) {
          TRACE("Failed due to different store addr!\n");
          return nullptr;
        }

        PackNode* TE = BuildPackNode(node_group, 2, 1, recursion_depth);
        PopStack();
        return TE;
      }
    default:
      TRACE("Default branch #%d:%s\n", node0->id(), node0->op()->mnemonic());
      break;
  }
  return nullptr;
}

void SLPTree::DeleteTree() {
  ClearStack();
  node_to_packnode_.clear();
}

void SLPTree::Print(const char* info) {
  TRACE("%s, Packed node:\n", info);
  if (!FLAG_trace_wasm_revectorize) return;
  std::unordered_set<PackNode const*> visited;

  for (auto& entry : node_to_packnode_) {
    PackNode const* pnode = entry.second;
    if (!pnode || visited.find(pnode) != visited.end()) continue;
    if (pnode->RevectorizedNode != nullptr) {
      TRACE("0x%p #%d:%s(%d %d, %s)\n", pnode, pnode->RevectorizedNode->id(),
            pnode->RevectorizedNode->op()->mnemonic(), pnode->Nodes[0]->id(),
            pnode->Nodes[1]->id(), pnode->Nodes[0]->op()->mnemonic());
    } else {
      TRACE("0x%p null(%d %d, %s)\n", pnode, pnode->Nodes[0]->id(),
            pnode->Nodes[1]->id(), pnode->Nodes[0]->op()->mnemonic());
    }

    visited.insert(pnode);
  }
}

//////////////////////////////////////////////////////
void Revectorizer::DetectCPUFeatures() {
  base::CPU cpu;
  if (cpu.has_avx() && cpu.has_osxsave()) {
    //&& OSHasAVXSupport()) {
    has_avx_ = true;
  }
  if (cpu.has_avx2()) {
    has_avx2_ = true;
  }
}

bool Revectorizer::TryRevectorize(const char* function) {
  bool success = false;
  if (graph_->HasSimd() && graph_->GetStoreNodes().size()) {
    TRACE("TryRevectorize %s\n", function);
    CollectSeeds();
    for (auto entry : group_of_stores_) {
      ZoneMap<Node*, StoreNodeSet>* store_chains = entry.second;
      if (store_chains != nullptr) {
        PrintStores(store_chains);
        if (ReduceStoreChains(store_chains)) {
          TRACE("Successful revectorize %s\n", function);
          success = true;
        }
      }
    }

    TRACE("Finish revectorize %s\n", function);
  }
  return success;
}

void Revectorizer::CollectSeeds() {
  for (auto it = graph_->GetStoreNodes().begin();
       it != graph_->GetStoreNodes().end(); ++it) {
    Node* node = *it;
    if ((node->opcode() == IrOpcode::kStore ||
         node->opcode() == IrOpcode::kProtectedStore) &&
        (StoreRepresentationOf(node->op()).representation() ==
         MachineRepresentation::kSimd128)) {
      Node* dominator = slp_tree_->GetEarlySchedulePosition(node);

      if ((GetMemoryOffsetValue(node) % 16) != 0) {
        continue;
      }
      Node* addr = GetNodeAddr(node);

      ZoneMap<Node*, StoreNodeSet>* mapping;
      auto entry1 = group_of_stores_.find(dominator);
      if (entry1 == group_of_stores_.end()) {
        mapping = zone_->New<ZoneMap<Node*, StoreNodeSet>>(zone_);
        group_of_stores_[dominator] = mapping;
      } else {
        mapping = entry1->second;
      }
      // If there was an existing entry, we would have found it earlier.
      // DCHECK_EQ(mapping->find(addr), mapping->end());
      // mapping->insert({addr, result});

      auto entry2 = mapping->find(addr);
      if (entry2 == mapping->end()) {
        entry2 = mapping->insert({addr, StoreNodeSet(zone())}).first;
      }
      entry2->second.insert(node);
    }
  }
}

bool Revectorizer::ReduceStoreChains(
    ZoneMap<Node*, StoreNodeSet>* store_chains) {
  TRACE("Enter %s\n", __func__);

  bool changed = false;

  for (auto it = store_chains->cbegin(); it != store_chains->cend(); ++it) {
    if (it->second.size() >= 2 && it->second.size() % 2 == 0) {
      // TRACE("Seed size = %lu, >= 2\n", it->second.size());
      std::vector<Node*> store_chain(it->second.begin(), it->second.end());

      for (auto it = store_chain.begin(); it < store_chain.end(); it = it + 2) {
        std::vector<Node*> stores(it, it + 2);
        if (ReduceStoreChain(stores)) {
          changed = true;
        }
      }
    }
  }

  return changed;
}

bool Revectorizer::ReduceStoreChain(const std::vector<Node*>& Stores) {
  TRACE("Enter %s, root@ (#%d,#%d)\n", __func__, Stores[0]->id(),
        Stores[1]->id());

  if (!IsContinuousAccess(Stores)) {
    return false;
  }

  PackNode* root = slp_tree_->BuildTree(Stores);
  if (!root) {
    TRACE("Build tree failed!\n");
    return false;
  }

  slp_tree_->Print("After build tree");
  TRACE("\n");
  return true;
}

// utility
void Revectorizer::PrintStores(ZoneMap<Node*, StoreNodeSet>* store_chains) {
  if (!FLAG_trace_wasm_revectorize) return;
  TRACE("Enter %s\n", __func__);
  for (auto it = store_chains->cbegin(); it != store_chains->cend(); ++it) {
    if (it->second.size() > 0) {
      TRACE("addr = #%d:%s \n", it->first->id(), it->first->op()->mnemonic());

      for (auto node : it->second) {
        TRACE("#%d:%s, ", node->id(), node->op()->mnemonic());
      }

      TRACE("\n");
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
