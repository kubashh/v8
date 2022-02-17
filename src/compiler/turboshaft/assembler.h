// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_ASSEMBLER_H_
#define V8_COMPILER_TURBOSHAFT_ASSEMBLER_H_

#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>

#include "src/base/iterator.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/small-vector.h"
#include "src/base/template-utils.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/intrusive-priority-queue.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

class Assembler;
class VarAssembler;

// template <class Derived>
class Assembler {
 public:
  Block* NewBlock(Block::Kind kind) { return graph_.NewBlock(kind); }

  bool Bind(Block* block) {
    DCHECK_EQ(block->graph, &this->graph_);
    DCHECK(block->kind <= Block::Kind::kBranchTarget);
    if (!graph().Add(block)) return false;
    DCHECK_NULL(current_block_);
    current_block_ = block;
    return true;
  }
#define EMIT_OP(Name)               \
  template <class... Args>          \
  OpIndex Name(Args... args) {      \
    return Emit<Name##Op>(args...); \
  }
  TURBOSHAFT_OPERATION_LIST(EMIT_OP)
#undef EMIT_OP

  explicit Assembler(Graph* graph, Zone* phase_zone)
      : graph_(*graph), phase_zone_(phase_zone) {
    graph_.Reset();
  }

  Block* current_block() { return current_block_; }
  Zone* graph_zone() { return graph().graph_zone(); }
  Graph& graph() { return graph_; }
  Zone* phase_zone() { return phase_zone_; }

 private:
  void FinalizeBlock() {
    DCHECK(!current_block_->end.valid());
    current_block_->end = graph().next_operation_index();
    current_block_ = nullptr;
  }

  template <class Op, class... Args>
  OpIndex Emit(Args... args) {
    STATIC_ASSERT((std::is_base_of<Operation, Op>::value));
    STATIC_ASSERT(!(std::is_same<Op, Operation>::value));
    DCHECK_NOT_NULL(current_block_);
    OpIndex result = graph().Add<Op>(args...);
    if (Op::properties.is_block_terminator) FinalizeBlock();
    return result;
  }

  Block* current_block_ = nullptr;
  Graph& graph_;
  Zone* const phase_zone_;
};

inline OperationStorageSlot* AllocateOpStorage(Graph* graph,
                                               size_t slot_count) {
  return graph->Allocate(slot_count);
}

template <class Subclass, class Superclass>
class AssemblerInterface : public Superclass {
 public:
  using Superclass::Superclass;
  using Base = Superclass;
  OpIndex Add(OpIndex left, OpIndex right, MachineRepresentation rep) {
    return static_cast<Subclass*>(this)->Binary(
        left, right, BinaryOp::Kind::kAdd, false, rep);
  }
  OpIndex AddWithOverflow(OpIndex left, OpIndex right,
                          MachineRepresentation rep) {
    DCHECK(rep == MachineRepresentation::kWord32 ||
           rep == MachineRepresentation::kWord64);
    return static_cast<Subclass*>(this)->Binary(
        left, right, BinaryOp::Kind::kAdd, true, rep);
  }
  OpIndex Sub(OpIndex left, OpIndex right, MachineRepresentation rep) {
    DCHECK(rep == MachineRepresentation::kWord32 ||
           rep == MachineRepresentation::kWord64);
    return static_cast<Subclass*>(this)->Binary(
        left, right, BinaryOp::Kind::kSub, false, rep);
  }
  OpIndex SubWithOverflow(OpIndex left, OpIndex right,
                          MachineRepresentation rep) {
    return static_cast<Subclass*>(this)->Binary(
        left, right, BinaryOp::Kind::kSub, true, rep);
  }
  OpIndex Mul(OpIndex left, OpIndex right, MachineRepresentation rep) {
    return static_cast<Subclass*>(this)->Binary(
        left, right, BinaryOp::Kind::kMul, false, rep);
  }
  OpIndex MulWithOverflow(OpIndex left, OpIndex right,
                          MachineRepresentation rep) {
    DCHECK(rep == MachineRepresentation::kWord32 ||
           rep == MachineRepresentation::kWord64);
    return static_cast<Subclass*>(this)->Binary(
        left, right, BinaryOp::Kind::kMul, true, rep);
  }
  OpIndex BitwiseAnd(OpIndex left, OpIndex right, MachineRepresentation rep) {
    DCHECK(rep == MachineRepresentation::kWord32 ||
           rep == MachineRepresentation::kWord64);
    return static_cast<Subclass*>(this)->Binary(
        left, right, BinaryOp::Kind::kBitwiseAnd, false, rep);
  }
  OpIndex BitwiseOr(OpIndex left, OpIndex right, MachineRepresentation rep) {
    DCHECK(rep == MachineRepresentation::kWord32 ||
           rep == MachineRepresentation::kWord64);
    return static_cast<Subclass*>(this)->Binary(
        left, right, BinaryOp::Kind::kBitwiseOr, false, rep);
  }
  OpIndex BitwiseXor(OpIndex left, OpIndex right, MachineRepresentation rep) {
    DCHECK(rep == MachineRepresentation::kWord32 ||
           rep == MachineRepresentation::kWord64);
    return static_cast<Subclass*>(this)->Binary(
        left, right, BinaryOp::Kind::kBitwiseXor, false, rep);
  }
};

class BasicAssembler : public AssemblerInterface<BasicAssembler, Assembler> {
 public:
  using AssemblerInterface::AssemblerInterface;

  OpIndex Phi(base::Vector<const OpIndex> inputs, MachineRepresentation rep) {
    DCHECK(current_block()->IsMerge() &&
           inputs.size() == current_block()->predecessors.size());
    return Base::Phi(inputs, rep);
  }

  template <class... Args>
  OpIndex PendingLoopPhi(Args... args) {
    DCHECK(current_block()->IsLoop());
    return Base::PendingLoopPhi(args...);
  }

  OpIndex Goto(Block* destination) {
    destination->AddPredecessor(current_block());
    DCHECK(current_block()->successors.empty());
    current_block()->successors = base::make_array(destination);
    return Base::Goto(destination);
  }

  OpIndex Branch(OpIndex condition, Block* if_true, Block* if_false) {
    if_true->AddPredecessor(current_block());
    if_false->AddPredecessor(current_block());
    DCHECK(current_block()->successors.empty());
    current_block()->successors = base::make_array(if_true, if_false);
    return Base::Branch(condition, if_true, if_false);
  }

  OpIndex Switch(OpIndex input, base::Vector<const SwitchOp::Case> cases,
                 Block* default_case) {
    DCHECK(current_block()->successors.empty());
    for (SwitchOp::Case c : cases) {
      c.destination->AddPredecessor(current_block());
      current_block()->successors.push_back(c.destination);
    }
    default_case->AddPredecessor(current_block());
    current_block()->successors.push_back(default_case);
    return Base::Switch(input, cases, default_case);
  }
};

class Variable : public IntrusivePriorityQueue::Item {
  struct Assignment {
    Block* block;
    OpIndex value;
  };

 public:
  explicit Variable(VarAssembler* assembler, MachineRepresentation rep);
  ~Variable();

  OpIndex LookupValue(Block* block) {
    for (auto it = assignments_.rbegin(); it != assignments_.rend(); ++it) {
      auto& assignment = *it;
      if (block->IsDominatedBy(assignment.block)) {
        return assignment.value;
      }
    }
    return OpIndex::Invalid();
  }

  MachineRepresentation rep() const { return rep_; }

 private:
  friend class VarAssembler;
  VarAssembler* assembler_;
  ZoneVector<Assignment> assignments_;
  const MachineRepresentation rep_;
};

class VarAssembler : public BasicAssembler {
 public:
  using Base = BasicAssembler;
  using Base::Base;

  void Write(Variable* variable, OpIndex value) {
    variable->assignments_.push_back({current_block(), value});
    variables_.AddOrUpdate(variable, graph().next_operation_index().offset());
  }

  OpIndex Read(Variable* variable) {
    return variable->LookupValue(current_block());
  }

  bool Bind(Block* block) {
    if (!Base::Bind(block)) return false;
    if (block->IsLoop()) {
      DCHECK_EQ(block->predecessors.size(), 1);
      Block* predecessor = block->predecessors[0];
      for (Variable* var : variables_) {
        OpIndex value = var->LookupValue(predecessor);
        if (value.valid()) {
          Write(var, PendingVariableLoopPhi(value, var));
        }
      }
    } else if (block->IsMerge()) {
      if (block->predecessors.size() <= 1) return true;
      OpIndex dominating_pos = block->immediate_dominator->end;
      // Only look at the variables that changed since the block dominating the
      // merge. Otherwise, the old value from this block is still good and we
      // don't need a new phi.
      for (Variable* var : variables_.MinRange(dominating_pos.offset())) {
        OpIndex first = var->LookupValue(block->predecessors[0]);
        bool needs_phi = false;
        bool is_valid = first.valid();
        base::SmallVector<OpIndex, 8> inputs = base::make_array(first);
        for (auto it = block->predecessors.begin() + 1;
             it != block->predecessors.end(); ++it) {
          OpIndex value = var->LookupValue(*it);
          needs_phi |= value != first;
          is_valid &= value.valid();
          inputs.emplace_back(value);
        }
        if (needs_phi && is_valid) {
          Write(var, Phi(base::VectorOf(inputs), var->rep()));
        }
      }
    }
    return true;
  }

  OpIndex Goto(Block* destination) {
    if (destination->IsLoop()) {
      FixLoopPhis(destination, current_block());
    }
    return Base::Goto(destination);
  }

  explicit VarAssembler(Graph* graph, Zone* phase_zone)
      : Base(graph, phase_zone), variables_(graph->graph_zone()) {}

 private:
  void FixLoopPhis(Block* loop, Block* backedge) {
    DCHECK(loop->IsLoop());
    for (Operation& op : graph().operations(*loop)) {
      if (auto* pending_phi = op.TryCast<PendingVariableLoopPhiOp>()) {
        Variable* var = pending_phi->variable;
        OpIndex first = pending_phi->first();
        OpIndex second = var->LookupValue(backedge);
        graph().Replace<PhiOp>(graph().Index(*pending_phi),
                               base::VectorOf({first, second}),
                               pending_phi->variable->rep());
      }
    }
  }

  friend class Variable;
  IntrusivePriorityQueueTempl<Variable> variables_;
};

inline Variable::Variable(VarAssembler* assembler, MachineRepresentation rep)
    : assembler_(assembler),
      assignments_(assembler_->graph_zone()),
      rep_(rep) {}
inline Variable::~Variable() { assembler_->variables_.Remove(this); }

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_ASSEMBLER_H_
