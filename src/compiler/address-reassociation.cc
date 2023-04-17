// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/address-reassociation.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/machine-graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

// Address reassociation
//
// The purpose of this transform is to pattern match certain address
// computations and reorganize the operands for more efficient code generation.
//
// Many addresses will be computed in the form like this:
// ProtectedLoad (IntPtrAdd (base_reg, immediate_offset ), register_offset ...

// And this pass aims to transform this into:
// ProtectedLoad (IntPtrAdd (base_reg, register_offset), immediate_offset ...
//
// This allows the reuse of a base pointer across multiple instructions, each of
// which then has the opportunity to use immediate offset indexing.

AddressReassociation::AddressReassociation(JSGraph* jsgraph, Zone* zone)
    : graph_(jsgraph->graph()),
      common_(jsgraph->common()),
      machine_(jsgraph->machine()),
      candidate_objects_(zone),
      candidates_(zone),
      zone_(zone) {}

void AddressReassociation::Optimize() {
  for (auto& candidate : candidates_) {
    const CandidateAddressKey& key = candidate.first;
    if (!ShouldTryOptimize(key)) continue;
    // We've found multiple instances of addresses in the form
    // object(base + imm_offset), reg_offset
    // So, create a new object for these operations to share and then use an
    // immediate index:
    // object(base, reg_offset), imm_offset
    Node* new_object = CreateNewBase(key);
    CandidateLoads& loads = candidate.second;
    size_t num_nodes = loads.GetNumNodes();
    for (size_t i = 0; i < num_nodes; ++i) {
      Node* mem_op = loads.mem_op(i);
      Node* index = loads.imm_offset(i);
      ReplaceInputs(mem_op, new_object, index);
    }
  }
}

bool AddressReassociation::ShouldTryOptimize(
    const CandidateAddressKey& key) const {
  // We already process the graph in terms of effect chains in an attempt to
  // reduce the risk of creating large live-ranges, but also set a lower
  // bound for the number of required users so that the benefits are more
  // likely to outweigh any detrimental affects. Benchmarking showed three
  // or more was a good heuristic.
  return candidates_.at(key).GetNumNodes() > 2;
}

Node* AddressReassociation::CreateNewBase(const CandidateAddressKey& key) {
  CandidateObject& candidate_object = candidate_objects_.at(key);
  Node* base = candidate_object.base();
  Node* reg_offset = candidate_object.offset();
  const Operator* op =
      machine_->Is32() ? machine_->Int32Add() : machine_->Int64Add();
  return graph_->NewNode(op, base, reg_offset);
}

void AddressReassociation::ReplaceInputs(Node* mem_op, Node* object,
                                         Node* index) {
  DCHECK_GT(mem_op->InputCount(), 1);
  DCHECK(NodeProperties::IsConstant(index));
  mem_op->ReplaceInput(0, object);
  mem_op->ReplaceInput(1, index);
}

void AddressReassociation::VisitLoad(Node* node, NodeId effect_chain) {
  DCHECK_GT(node->inputs().count(), 1);

  // Inspect the base component
  if (node->InputAt(0)->opcode() == IrOpcode::kInt32Add) {
    DCHECK(machine_->Is32());
    Int32BinopMatcher madd(node->InputAt(0));
    if (madd.right().HasResolvedValue()) {
      return VisitLoadBaseComponent(node, madd.node(), effect_chain);
    }
  } else if (node->InputAt(0)->opcode() == IrOpcode::kInt64Add) {
    Int64BinopMatcher madd(node->InputAt(0));
    if (madd.right().HasResolvedValue()) {
      return VisitLoadBaseComponent(node, madd.node(), effect_chain);
    }
  }

  // Inspect the index.
  if (node->InputAt(1)->opcode() == IrOpcode::kInt32Add) {
    DCHECK(machine_->Is32());
    Int32BinopMatcher madd(node->InputAt(1));
    if (madd.right().HasResolvedValue()) {
      return VisitLoadIndexComponent(node, madd.node(), effect_chain);
    }
  } else if (node->InputAt(1)->opcode() == IrOpcode::kInt64Add) {
    Int64BinopMatcher madd(node->InputAt(1));
    if (madd.right().HasResolvedValue()) {
      return VisitLoadIndexComponent(node, madd.node(), effect_chain);
    }
  }
}

void AddressReassociation::VisitLoadBaseComponent(Node* load, Node* add,
                                                  NodeId effect_chain) {
  DCHECK(add->opcode() == IrOpcode::kInt32Add ||
         add->opcode() == IrOpcode::kInt64Add);
  Node* base = add->InputAt(0);
  Node* imm_offset = add->InputAt(1);
  Node* reg_offset = load->InputAt(1);
  AddLoadCandidate(load, base, reg_offset, imm_offset, effect_chain);
}

void AddressReassociation::VisitLoadIndexComponent(Node* load, Node* add,
                                                   NodeId effect_chain) {
  DCHECK(add->opcode() == IrOpcode::kInt32Add ||
         add->opcode() == IrOpcode::kInt64Add);
  Node* base = load->InputAt(0);
  Node* reg_offset = add->InputAt(0);
  Node* imm_offset = add->InputAt(1);
  AddLoadCandidate(load, base, reg_offset, imm_offset, effect_chain);
}

void AddressReassociation::AddLoadCandidate(Node* load, Node* base_reg,
                                            Node* offset_reg, Node* imm_offset,
                                            NodeId effect_chain) {
  DCHECK(load->opcode() == IrOpcode::kProtectedLoad);
  DCHECK(NodeProperties::IsConstant(imm_offset));
  CandidateAddressKey key =
      std::make_tuple(base_reg->id(), offset_reg->id(), effect_chain);
  if (!HasCandidateObject(key)) {
    candidate_objects_.emplace(key, CandidateObject(base_reg, offset_reg));
    candidates_.emplace(key, CandidateLoads(zone_));
  }
  candidates_.at(key).AddCandidate(load, imm_offset);
}

bool AddressReassociation::HasCandidateObject(
    const CandidateAddressKey& key) const {
  return candidate_objects_.count(key);
}

void AddressReassociation::CandidateLoads::AddCandidate(Node* mem_op,
                                                        Node* imm_offset) {
  DCHECK(NodeProperties::IsConstant(imm_offset));
  mem_ops_.push_back(mem_op);
  imm_offsets_.push_back(imm_offset);
}

size_t AddressReassociation::CandidateLoads::GetNumNodes() const {
  DCHECK_EQ(mem_ops_.size(), imm_offsets_.size());
  return mem_ops_.size();
}

Node* AddressReassociation::CandidateLoads::mem_op(size_t i) const {
  DCHECK_LT(i, mem_ops_.size());
  return mem_ops_[i];
}

Node* AddressReassociation::CandidateLoads::imm_offset(size_t i) const {
  DCHECK_LT(i, imm_offsets_.size());
  return imm_offsets_[i];
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
