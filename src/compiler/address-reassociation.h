// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_ADDRESS_REASSOCIATION_H_
#define V8_COMPILER_ADDRESS_REASSOCIATION_H_

#include "src/compiler/node-marker.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
class Graph;
class JSGraph;
class MachineOperatorBuilder;
class Node;

class V8_EXPORT_PRIVATE AddressReassociation final {
  // Use the ids of nodes that represent a base and offset, together with an
  // effect-chain root node id, to create a key for our candidate maps.
  using CandidateAddressKey = std::tuple<NodeId, NodeId, NodeId>;

  // Holds two nodes that could be summed to create a new object address. We
  // store these in a map accessed with the above key.
  class CandidateObject {
   public:
    CandidateObject(Node* base, Node* offset)
        : base_reg_(base), offset_reg_(offset) {}
    Node* base() const { return base_reg_; }
    Node* offset() const { return offset_reg_; }

   private:
    Node* base_reg_;
    Node* offset_reg_;
  };

  // For a given CandidateObject, collect loads that could use the shared
  // object along an immediate index. These are collected in a map which is
  // accessed with a CandidateAddressKey.
  class CandidateLoads : ZoneObject {
   public:
    CandidateLoads(Zone* zone) : mem_ops_(zone), imm_offsets_(zone) {}
    void AddCandidate(Node* mem_op, Node* imm_offset);
    size_t GetNumNodes() const;
    Node* mem_op(size_t i) const;
    Node* imm_offset(size_t i) const;

   private:
    ZoneVector<Node*> mem_ops_;
    ZoneVector<Node*> imm_offsets_;
  };

 public:
  AddressReassociation(JSGraph* jsgraph, Zone* zone);
  AddressReassociation(const AddressReassociation&) = delete;
  AddressReassociation& operator=(const AddressReassociation&) = delete;

  void Optimize();
  void VisitLoad(Node* node, NodeId effect_chain);

 private:
  void VisitLoadBaseComponent(Node* load, Node* add, NodeId effect_chain);
  void VisitLoadIndexComponent(Node* load, Node* add, NodeId effect_chain);
  bool ShouldTryOptimize(const CandidateAddressKey& key) const;
  Node* CreateNewBase(const CandidateAddressKey& key);
  bool HasCandidateObject(const CandidateAddressKey& key) const;
  void AddLoadCandidate(Node* load, Node* base_reg, Node* offset_reg,
                        Node* imm_offset, NodeId effect_chain);
  void ReplaceInputs(Node* mem_op, Node* object, Node* index);

  Graph* const graph_;
  CommonOperatorBuilder* common_;
  MachineOperatorBuilder* machine_;
  ZoneMap<CandidateAddressKey, CandidateObject> candidate_objects_;
  ZoneMap<CandidateAddressKey, CandidateLoads> candidates_;
  Zone* const zone_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_ADDRESS_REASSOCIATION_H_
