// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/load-store-folding.h"

#include "src/compiler/node-matchers.h"
#include "src/compiler/verifier.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE(fmt, ...)                                     \
  do {                                                      \
    if (FLAG_trace_load_store_folding) {                    \
      PrintF("LoadStoreFolding: " fmt "\n", ##__VA_ARGS__); \
    }                                                       \
  } while (false)

int LoadStoreFolding::ReduceStores(LoadStorePairCandidate& pair_node,
                                   LoadStorePairCandidate& other_node,
                                   bool pair_is_lo_offset) {
  if (pair_is_lo_offset) {
    // Insert other_node's value input into the second storage position.
    pair_node.node->InsertInput(jsgraph_->zone(), 3,
                                other_node.node->InputAt(2));
  } else {
    // Ensure pair_node has the lowest offset value of the pair.
    pair_node.node->ReplaceInput(1, other_node.node->InputAt(1));
    // Insert other_node's value input into the first storage position.
    pair_node.node->InsertInput(jsgraph_->zone(), 2,
                                other_node.node->InputAt(2));
  }

  StoreRepresentation pair_store_rep =
      StoreRepresentationOf(pair_node.node->op());
  StoreRepresentation other_store_rep =
      StoreRepresentationOf(other_node.node->op());
  TRACE("  Reducing stores #%d:%s (%s), #%d:%s (%s)", pair_node.node->id(),
        pair_node.node->op()->mnemonic(),
        MachineReprToString(pair_store_rep.representation()),
        other_node.node->id(), other_node.node->op()->mnemonic(),
        MachineReprToString(other_store_rep.representation()));

  if (IsAnyCompressed(other_store_rep.representation())) {
    // Use the compressed representation if it exists to make sure
    // the required checks are performed.
    pair_store_rep = other_store_rep;
  }
  NodeProperties::ChangeOp(pair_node.node,
                           jsgraph_->machine()->StorePair(pair_store_rep));

  // Remove other_node from the effect chain before killing it.
  DCHECK_EQ(other_node.node->UseCount(), 1);
  Edge edge = *other_node.node->use_edges().begin();
  DCHECK(NodeProperties::IsEffectEdge(edge));
  Verifier::VerifyEdgeInputReplacement(
      edge, NodeProperties::GetEffectInput(other_node.node));
  edge.UpdateTo(NodeProperties::GetEffectInput(other_node.node));
  other_node.node->Kill();
  return ElementSizeLog2Of(pair_store_rep.representation());
}

int LoadStoreFolding::ReduceLoads(LoadStorePairCandidate& pair_node,
                                  LoadStorePairCandidate& other_node,
                                  bool pair_is_lo_offset) {
  // The load pair has two outputs so requires a projection for each output. The
  // projection indexes directly tie to their ordering in memory.
  const Operator* other_proj =
      jsgraph_->common()->Projection(pair_is_lo_offset ? 1 : 0);
  const Operator* pair_proj =
      jsgraph_->common()->Projection(pair_is_lo_offset ? 0 : 1);

  if (!pair_is_lo_offset) {
    // Ensure pair_node has the lowest offset value of the pair.
    pair_node.node->ReplaceInput(1, other_node.node->InputAt(1));
  }

  LoadRepresentation other_rep = LoadRepresentationOf(other_node.node->op());
  LoadRepresentation pair_rep = LoadRepresentationOf(pair_node.node->op());
  TRACE("  Reducing loads #%d:%s (%s), #%d:%s (%s)", other_node.node->id(),
        other_node.node->op()->mnemonic(),
        MachineReprToString(other_rep.representation()), pair_node.node->id(),
        pair_node.node->op()->mnemonic(),
        MachineReprToString(pair_rep.representation()));

  if (other_rep == pair_rep) {
    NodeProperties::ChangeOp(pair_node.node,
                             jsgraph_->machine()->LoadPair(pair_rep));
  } else if (pair_is_lo_offset) {
    NodeProperties::ChangeOp(
        pair_node.node, jsgraph_->machine()->LoadPair(pair_rep, other_rep));
  } else {
    NodeProperties::ChangeOp(
        pair_node.node, jsgraph_->machine()->LoadPair(other_rep, pair_rep));
  }

  // Reconnect the old loads' uses to the new projection nodes.
  Node* projection_inputs[2] = {
      pair_node.node, NodeProperties::GetControlInput(pair_node.node)};
  Node* pair_proj_node =
      jsgraph_->graph()->NewNode(pair_proj, 2, projection_inputs);
  for (Edge edge : pair_node.node->use_edges()) {
    if (edge.from() == pair_proj_node || NodeProperties::IsEffectEdge(edge)) {
      continue;
    }
    Verifier::VerifyEdgeInputReplacement(edge, pair_proj_node);
    edge.UpdateTo(pair_proj_node);
  }

  Node* other_proj_node =
      jsgraph_->graph()->NewNode(other_proj, 2, projection_inputs);
  for (Edge edge : other_node.node->use_edges()) {
    if (NodeProperties::IsEffectEdge(edge)) {
      // Remove other_node from the effect chain.
      Verifier::VerifyEdgeInputReplacement(
          edge, NodeProperties::GetEffectInput(other_node.node));
      edge.UpdateTo(NodeProperties::GetEffectInput(other_node.node));
      continue;
    }
    Verifier::VerifyEdgeInputReplacement(edge, other_proj_node);
    edge.UpdateTo(other_proj_node);
  }
  other_node.node->Kill();
  TRACE("  Added #%d:%s, #%d:%s", other_proj_node->id(),
        other_proj_node->op()->mnemonic(), pair_proj_node->id(),
        pair_proj_node->op()->mnemonic());
  return ElementSizeLog2Of(pair_rep.representation());
}

void LoadStoreFolding::UseSharedSubForOffset(
    Node* pair_node, ZoneMap<NodeId, Node*>& aligned_offset_bases,
    int64_t offset) {
  Node*& aligned_offset_base =
      aligned_offset_bases[pair_node->InputAt(0)->id()];
  if (aligned_offset_base->opcode() != IrOpcode::kInt64Sub) {
    // Bitcast the original offset base and subtract one to align the offset.
    Node* bitcast_inputs[3] = {pair_node->InputAt(0),
                               NodeProperties::GetEffectInput(pair_node),
                               NodeProperties::GetControlInput(pair_node)};
    Node* bitcast_node = jsgraph_->graph()->NewNode(
        jsgraph_->machine()->BitcastTaggedToWord(), 3, bitcast_inputs);
    // Place bitcast_node in the effect chain directly above pair_node.
    NodeProperties::ReplaceEffectInput(pair_node, bitcast_node);
    Node* sub_inputs[2] = {
        bitcast_node, jsgraph_->graph()->NewNode(
                          jsgraph_->common()->Int64Constant(1), 0, nullptr)};
    aligned_offset_base = jsgraph_->graph()->NewNode(
        jsgraph_->machine()->IntSub(), 2, sub_inputs);
  }
  TRACE("    Use #%d:%s and #%d:%s", aligned_offset_base->id(),
        aligned_offset_base->op()->mnemonic(),
        aligned_offset_base->InputAt(0)->id(),
        aligned_offset_base->InputAt(0)->op()->mnemonic());
  // Replace (orig_offset_base + offset_imm) with
  // ((orig_offset_base - 1) + (offset_imm + 1)).
  pair_node->ReplaceInput(0, aligned_offset_base);
  pair_node->ReplaceInput(
      1, jsgraph_->graph()->NewNode(
             jsgraph_->common()->Int64Constant(offset + 1), 0, nullptr));
}

void LoadStoreFolding::VisitPairs(
    ZoneSet<std::pair<LoadStorePairCandidate, LoadStorePairCandidate>,
            IndexCompare>& pairs,
    ZoneMap<NodeId, Node*>& aligned_offset_bases) {
  for (std::pair<LoadStorePairCandidate, LoadStorePairCandidate> pair : pairs) {
    auto it = aligned_offset_bases.find(pair.first.node->InputAt(0)->id());
    // Do not create a pair if there is no other pair to share a sub with.
    if (it == aligned_offset_bases.end() || it->second == nullptr) continue;
    int rep_log_size;
    bool pair_is_lo_offset = pair.first.offset < pair.second.offset;
    if (pair.first.node->opcode() == IrOpcode::kStore) {
      DCHECK_EQ(pair.second.node->opcode(), IrOpcode::kStore);
      rep_log_size = ReduceStores(pair.first, pair.second, pair_is_lo_offset);
    } else {
      DCHECK(pair.first.node->opcode() == IrOpcode::kLoad ||
             pair.first.node->opcode() == IrOpcode::kLoadImmutable);
      DCHECK(pair.second.node->opcode() == IrOpcode::kLoad ||
             pair.second.node->opcode() == IrOpcode::kLoadImmutable);
      rep_log_size = ReduceLoads(pair.first, pair.second, pair_is_lo_offset);
    }
    int64_t offset = std::min(pair.first.offset, pair.second.offset);
    int rem = offset % static_cast<int>(1 << rep_log_size);
    if ((rem == -1 || rem == (1 << rep_log_size) - 1) &&
        is_int7((offset + 1) >> rep_log_size)) {
      DCHECK((pair.first.node->opcode() == IrOpcode::kLoadPair) ||
             (pair.first.node->opcode() == IrOpcode::kStorePair));
      UseSharedSubForOffset(pair.first.node, aligned_offset_bases, offset);
    }
  }
}

bool LoadStoreFolding::AddCandidatesToPairs(
    const LoadStorePairCandidate& candidate,
    const LoadStorePairCandidate* prev_candidate, int rep_log_size,
    ZoneSet<std::pair<LoadStorePairCandidate, LoadStorePairCandidate>,
            IndexCompare>& pairs,
    ZoneMap<NodeId, Node*>& aligned_offset_bases) {
  int rep_size = 1 << rep_log_size;
  if (prev_candidate &&
      (candidate.offset == prev_candidate->offset + rep_size)) {
    int offset_base_id = candidate.node->InputAt(0)->id();
    // Use a shared offset base node if offset not aligned.
    int rem = prev_candidate->offset % rep_size;
    if (rem != 0 && is_int9(prev_candidate->offset)) {
      if ((rem == -1 || rem == rep_size - 1) &&
          is_int7((prev_candidate->offset + 1) >> rep_log_size)) {
        auto it = aligned_offset_bases.find(offset_base_id);
        if (it == aligned_offset_bases.end()) {
          // Mark offset_base as used once.
          aligned_offset_bases[offset_base_id] = nullptr;
        } else {
          // Mark offset_base as used more than once using arbitrary value.
          aligned_offset_bases[offset_base_id] = candidate.node;
        }
      } else {
        // Do not create a pair if we cannot share a sub.
        return false;
      }
    } else {
      // Always create a pair if no address calculation is required for the pair
      // or if address calculation is required for the existing load/store.
      aligned_offset_bases[offset_base_id] = candidate.node;
    }
    // Order candidates by which node will become the pair node.
    if ((candidate.node->opcode() == IrOpcode::kStore) ==
        (candidate.chain_index < prev_candidate->chain_index)) {
      DCHECK_EQ(pairs.find(std::make_pair(candidate, *prev_candidate)),
                pairs.end());
      pairs.emplace(candidate, *prev_candidate);
    } else {
      DCHECK_EQ(pairs.find(std::make_pair(*prev_candidate, candidate)),
                pairs.end());
      pairs.emplace(*prev_candidate, candidate);
    }
    return true;
  }
  return false;
}

void LoadStoreFolding::VisitLoads(
    LoadsMap& loads,
    ZoneSet<std::pair<LoadStorePairCandidate, LoadStorePairCandidate>,
            IndexCompare>& pairs,
    ZoneMap<NodeId, Node*>& aligned_offset_bases) {
  for (const auto& it : loads) {
    const LoadStorePairCandidate* prev_candidate = nullptr;
    for (const LoadStorePairCandidate& candidate : it.second) {
      DCHECK_NE(candidate.node->opcode(), IrOpcode::kStore);
      if (!AddCandidatesToPairs(candidate, prev_candidate,
                                it.first.rep_log_size, pairs,
                                aligned_offset_bases)) {
        prev_candidate = &candidate;
      } else {
        prev_candidate = nullptr;
      }
    }
  }
}

bool LoadStoreFolding::IsValidLoadStoreRep(MachineRepresentation rep,
                                           int rep_log_size) {
  return (rep_log_size == 2 || rep_log_size == 3) && !IsFloatingPoint(rep);
}

bool LoadStoreFolding::VisitStorePair(
    Node* node, Node* prev_node,
    ZoneSet<std::pair<LoadStorePairCandidate, LoadStorePairCandidate>,
            IndexCompare>& pairs,
    ZoneMap<NodeId, Node*>& aligned_offset_bases, int chain_index) {
  DCHECK_EQ(node->opcode(), IrOpcode::kStore);
  DCHECK_EQ(prev_node->opcode(), IrOpcode::kStore);
  // Check offset bases match.
  if (node->InputAt(0) != prev_node->InputAt(0)) return false;
  StoreRepresentation store_rep = StoreRepresentationOf(node->op());
  MachineRepresentation rep = store_rep.representation();
  StoreRepresentation prev_store_rep = StoreRepresentationOf(prev_node->op());
  MachineRepresentation prev_rep = prev_store_rep.representation();
  // Check that element sizes are equal.
  int rep_log_size = ElementSizeLog2Of(rep);
  if (rep_log_size != ElementSizeLog2Of(prev_rep)) return false;
  if (!IsValidLoadStoreRep(rep, rep_log_size)) return false;
  // Check that both stores have no write barrier.
  WriteBarrierKind write_barrier_kind = store_rep.write_barrier_kind();
  if (!FLAG_disable_write_barriers &&
      (write_barrier_kind != kNoWriteBarrier ||
       (FLAG_enable_unconditional_write_barriers &&
        CanBeTaggedOrCompressedPointer(rep))))
    return false;
  WriteBarrierKind prev_write_barrier_kind =
      prev_store_rep.write_barrier_kind();
  if (!FLAG_disable_write_barriers &&
      (prev_write_barrier_kind != kNoWriteBarrier ||
       (FLAG_enable_unconditional_write_barriers &&
        CanBeTaggedOrCompressedPointer(prev_rep))))
    return false;
  // Check that both stores have resolved offset values.
  Node* offset = node->InputAt(1);
  IntPtrMatcher m(offset);
  if (!m.HasResolvedValue()) return false;
  Node* prev_offset = prev_node->InputAt(1);
  IntPtrMatcher prev_m(prev_offset);
  if (!prev_m.HasResolvedValue()) return false;

  LoadStorePairCandidate cand = {node, m.ResolvedValue(), chain_index};
  LoadStorePairCandidate prev_cand = {prev_node, prev_m.ResolvedValue(),
                                      chain_index - 1};
  if (m.ResolvedValue() > prev_m.ResolvedValue()) {
    return AddCandidatesToPairs(cand, &prev_cand, rep_log_size, pairs,
                                aligned_offset_bases);
  } else {
    return AddCandidatesToPairs(prev_cand, &cand, rep_log_size, pairs,
                                aligned_offset_bases);
  }
}

void LoadStoreFolding::AddNodeToLoads(Zone* zone, LoadsMap* map, Node* node,
                                      MachineRepresentation rep,
                                      int chain_index) {
  int rep_log_size = ElementSizeLog2Of(rep);
  if (!IsValidLoadStoreRep(rep, rep_log_size) ||
      node->InputAt(0)->UseCount() < 2)
    return;
  Node* offset = node->InputAt(1);
  IntPtrMatcher m(offset);
  if (!m.HasResolvedValue()) return;
  LoadKey key = {node->InputAt(0)->id(), ElementSizeLog2Of(rep)};
  auto it = map->find(key);
  if (it == map->end()) {
    it = map->emplace(key, LoadsByOffset(zone)).first;
  }
  TRACE("  add to consecutive map #%d:%s", node->id(), node->op()->mnemonic());
  DCHECK_EQ(it->second.find(
                LoadStorePairCandidate{node, m.ResolvedValue(), chain_index}),
            it->second.end());
  it->second.emplace(
      LoadStorePairCandidate{node, m.ResolvedValue(), chain_index});
}

void LoadStoreFolding::GetEffectChain(ZoneVector<Node*>& chain, Node* node) {
  while (true) {
    int effect_input_count = node->op()->EffectInputCount();
    if (effect_input_count > 1) {
      // Effect phi, break the chain and restart.
      CHECK_EQ(node->opcode(), IrOpcode::kEffectPhi);
      for (int i = 0; i < node->op()->EffectInputCount(); i++) {
        Node* input = NodeProperties::GetEffectInput(node, i);
        TRACE("chain break: effect phi break\nvisit later #%d:%s", input->id(),
              input->op()->mnemonic());
        to_visit_.push(input);
      }
      break;
    }
    if (effect_input_count == 0) {
      // This is the end!
      CHECK_EQ(node->opcode(), IrOpcode::kStart);
      TRACE("End");
      break;
    }
    if (node->opcode() == IrOpcode::kCall || NodeProperties::IsControl(node)) {
      Node* input = NodeProperties::GetEffectInput(node);
      TRACE("chain break: call/control\nvisit later #%d:%s", input->id(),
            input->op()->mnemonic());
      to_visit_.push(input);
      break;
    }
    if (chain.size() > 0) {
      int effect_edges = 0;
      for (Edge edge : node->use_edges()) {
        // Skip non-effect edges
        if (NodeProperties::IsEffectEdge(edge)) {
          effect_edges++;
        }
      }
      if (effect_edges > 1) {
        // Multiple effect edges, break the chain here and restart.
        Node* input = NodeProperties::GetEffectInput(node);
        TRACE("chain break: multiple effect edges\nvisit later #%d:%s",
              input->id(), input->op()->mnemonic());
        to_visit_.push(input);
        break;
      }
    }
    TRACE("  add to chain #%d:%s", node->id(), node->op()->mnemonic());
    chain.push_back(node);

    // Next!
    node = NodeProperties::GetEffectInput(node);
  }
}

void LoadStoreFolding::VisitEffectChain(Node* node) {
  TRACE("VisitEffectChain from #%d:%s", node->id(), node->op()->mnemonic());

  ZoneVector<Node*> chain(zone());
  GetEffectChain(chain, node);
  if (chain.size() > 1) {
    ZoneMap<NodeId, Node*> aligned_offset_bases(zone());
    ZoneSet<std::pair<LoadStorePairCandidate, LoadStorePairCandidate>,
            IndexCompare>
        pairs(zone());
    Node* prev_store = nullptr;
    LoadsMap loads(zone());
    // Track the type of the last node to avoid interleaved loads and stores.
    IrOpcode::Value prev_type = IrOpcode::kEnd;
    // Use index in chain to know which position to place the load/store pair.
    int chain_index = 0;
    for (Node* node : chain) {
      ++chain_index;
      if (node->opcode() == IrOpcode::kStore) {
        // Break the chain of loads if we encounter a store.
        if (prev_type == IrOpcode::kLoad) {
          VisitLoads(loads, pairs, aligned_offset_bases);
          loads.clear();
        }
        prev_type = IrOpcode::kStore;
        if (prev_store && VisitStorePair(node, prev_store, pairs,
                                         aligned_offset_bases, chain_index)) {
          prev_store = nullptr;
        } else {
          prev_store = node;
        }
      } else if (node->opcode() == IrOpcode::kLoad ||
                 node->opcode() == IrOpcode::kLoadImmutable) {
        prev_type = IrOpcode::kLoad;
        prev_store = nullptr;
        LoadRepresentation load_rep = LoadRepresentationOf(node->op());
        AddNodeToLoads(zone(), &loads, node, load_rep.representation(),
                       chain_index);
      }
    }
    VisitLoads(loads, pairs, aligned_offset_bases);
    VisitPairs(pairs, aligned_offset_bases);
  }
}

void LoadStoreFolding::Visit(Node* node) {
  if (have_visited_.find(node->id()) != have_visited_.end()) return;
  TRACE("Visit #%d:%s", node->id(), node->op()->mnemonic());
  if (NodeProperties::IsControl(node)) {
    for (int i = 0; i < node->op()->ControlInputCount(); i++) {
      Node* control_input = NodeProperties::GetControlInput(node, i);
      if (have_visited_.find(control_input->id()) == have_visited_.end()) {
        to_visit_.push(control_input);
      }
    }
  }
  if (node->op()->EffectInputCount() > 0) {
    VisitEffectChain(node);
  }
  have_visited_.insert(node->id());
}

void LoadStoreFolding::Run() {
  to_visit_.push(jsgraph()->graph()->end());
  while (!to_visit_.empty()) {
    Node* next = to_visit_.top();
    to_visit_.pop();
    Visit(next);
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
