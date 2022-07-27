// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_LOAD_STORE_FOLDING_H_
#define V8_COMPILER_LOAD_STORE_FOLDING_H_

#include "src/compiler/common-operator.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

class LoadStoreFolding final {
 public:
  LoadStoreFolding(JSGraph* jsgraph, Zone* zone, GraphReducer* reducer)
      : jsgraph_(jsgraph), zone_(zone), to_visit_(zone), have_visited_(zone) {}

  void Run();

 private:
  struct LoadStorePairCandidate {
    Node* node;
    int64_t offset;
    int chain_index;
  };

  struct OffsetCompare {
    bool operator()(const LoadStorePairCandidate& a,
                    const LoadStorePairCandidate& b) const {
      // Order by immediate offset
      return std::tie(a.offset, a.node) < std::tie(b.offset, b.node);
    }
  };

  struct IndexCompare {
    bool operator()(
        const std::pair<LoadStorePairCandidate, LoadStorePairCandidate>& a,
        const std::pair<LoadStorePairCandidate, LoadStorePairCandidate>& b)
        const {
      // Order by chain index
      return std::tie(a.first.chain_index, a.first.node) >
             std::tie(b.first.chain_index, b.first.node);
    }
  };

  struct LoadKey {
    NodeId id;
    int rep_log_size;
  };

  struct KeyCompare {
    bool operator()(const LoadKey& a, const LoadKey& b) const {
      return std::tie(a.id, a.rep_log_size) < std::tie(b.id, b.rep_log_size);
    }
  };

  using LoadsByOffset = ZoneSet<LoadStorePairCandidate, OffsetCompare>;
  using LoadsMap = ZoneMap<LoadKey, LoadsByOffset, KeyCompare>;

  int ReduceStores(LoadStorePairCandidate& pair_node,
                   LoadStorePairCandidate& other_node, bool pair_is_lo_offset);
  int ReduceLoads(LoadStorePairCandidate& pair_node,
                  LoadStorePairCandidate& other_node, bool pair_is_lo_offset);

  void UseSharedSubForOffset(Node* pair_node,
                             ZoneMap<NodeId, Node*>& aligned_offset_bases,
                             int64_t offset);

  void VisitPairs(
      ZoneSet<std::pair<LoadStorePairCandidate, LoadStorePairCandidate>,
              IndexCompare>& pairs,
      ZoneMap<NodeId, Node*>& aligned_offset_bases);
  bool AddCandidatesToPairs(
      const LoadStorePairCandidate& candidate,
      const LoadStorePairCandidate* prev_candidate, int rep_log_size,
      ZoneSet<std::pair<LoadStorePairCandidate, LoadStorePairCandidate>,
              IndexCompare>& pairs,
      ZoneMap<NodeId, Node*>& aligned_offset_bases);
  void VisitLoads(
      LoadsMap& loads,
      ZoneSet<std::pair<LoadStorePairCandidate, LoadStorePairCandidate>,
              IndexCompare>& pairs,
      ZoneMap<NodeId, Node*>& aligned_offset_bases);
  bool IsValidLoadStoreRep(MachineRepresentation rep, int rep_log_size);
  bool VisitStorePair(
      Node* node, Node* prev_node,
      ZoneSet<std::pair<LoadStorePairCandidate, LoadStorePairCandidate>,
              IndexCompare>& pairs,
      ZoneMap<NodeId, Node*>& aligned_offset_bases, int chain_index);
  void AddNodeToLoads(Zone* zone, LoadsMap* map, Node* node,
                      MachineRepresentation rep, int chain_index);

  void GetEffectChain(ZoneVector<Node*>& chain, Node* node);
  void VisitEffectChain(Node* node);
  void Visit(Node* node);

  JSGraph* jsgraph() const { return jsgraph_; }
  Zone* zone() const { return zone_; }

  JSGraph* const jsgraph_;
  Zone* const zone_;
  ZoneStack<Node*> to_visit_;
  ZoneSet<NodeId> have_visited_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_LOAD_STORE_FOLDING_H_
