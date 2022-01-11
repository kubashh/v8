// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_CFG_H_
#define V8_COMPILER_TURBOSHAFT_CFG_H_

#include <iterator>
#include <type_traits>

#include "src/base/iterator.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/small-vector.h"
#include "src/base/template-utils.h"
#include "src/compiler/turboshaft/intrusive-priority-queue.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

class Assembler;
class VarAssembler;

struct Block {
  enum class Kind { kMerge, kLoop, kBranch };

  Block(Kind kind, Zone* graph_zone)
      : kind(kind), predecessors(graph_zone), successors(graph_zone) {}

  bool IsLoopOrMerge() const { return IsLoop() || IsMerge(); }
  bool IsLoop() const { return kind == Kind::kLoop; }
  bool IsMerge() const { return kind == Kind::kMerge; }

  bool Contains(OpIndex op_idx) const {
    return op_idx >= first && op_idx <= last;
  }

  Block* CommonDominator(Block* other) {
    Block* self = this;
    int depth_diff = other->dominator_depth - self->dominator_depth;
    if (depth_diff < 0) {
      std::swap(self, other);
      depth_diff = -depth_diff;
    }
    for (; depth_diff > 0; --depth_diff) {
      other = other->immediate_dominator;
    }
    while (self != other) {
      self = self->immediate_dominator;
      other = other->immediate_dominator;
    }
    return self;
  }

  bool IsDominatedBy(const Block* dominator) const {
    const Block* block = this;
    for (int i = dominator_depth - dominator->dominator_depth; i > 0; --i) {
      block = block->immediate_dominator;
    }
    return block == dominator;
  }

  bool IsBound() const { return index != BlockIndex::kInvalid; }

  void AddPredecessor(Block* predecessor) {
    if (predecessors.empty()) {
      DCHECK(!IsBound());
      immediate_dominator = predecessor;
      dominator_depth = predecessor->dominator_depth + 1;
    } else {
      switch (kind) {
        case Kind::kMerge:
          DCHECK(!IsBound());
          immediate_dominator = CommonDominator(predecessor);
          dominator_depth = immediate_dominator->dominator_depth + 1;
          break;
        case Kind::kLoop:
          DCHECK(IsBound());
          DCHECK_EQ(predecessors.size(), 1);
          break;
        case Kind::kBranch:
          UNREACHABLE();
      }
    }
    predecessors.push_back(predecessor);
  }

  Kind kind;
  OpIndex first = OpIndex::kInvalid;
  OpIndex last = OpIndex::kInvalid;
  BlockIndex index = BlockIndex::kInvalid;
  Block* immediate_dominator = nullptr;
  int dominator_depth = 0;
  ZoneVector<Block*> predecessors;
  ZoneVector<Block*> successors;
#ifdef DEBUG
  Assembler* assembler = nullptr;
#endif
};

class Graph {
 public:
  explicit Graph(Zone* graph_zone)
      : operations_(graph_zone), blocks_(graph_zone), graph_zone_(graph_zone) {}

  const Operation& Get(OpIndex i) const {
    DCHECK_LT(ToUnderlyingType(i), operations_.size());
    return *reinterpret_cast<const Operation*>(
        &operations_[ToUnderlyingType(i)]);
  }

  const Block& StartBlock() const { return Get(static_cast<BlockIndex>(0)); }

  Block& Get(BlockIndex i) {
    DCHECK_LT(ToUnderlyingType(i), blocks_.size());
    return *blocks_[ToUnderlyingType(i)];
  }
  const Block& Get(BlockIndex i) const {
    DCHECK_LT(ToUnderlyingType(i), blocks_.size());
    return *blocks_[ToUnderlyingType(i)];
  }

  OpIndex Index(const Operation& op) const {
    size_t i = reinterpret_cast<const decltype(operations_)::value_type*>(&op) -
               operations_.data();
    DCHECK_LT(i, operations_.size());
    return static_cast<OpIndex>(i);
  }

  template <class Op>
  OpIndex Add(const Op& op) {
    DCHECK(InputsValid(op));
    OpIndex index = next_operation_index();
    operations_.emplace_back();
    new (&operations_.back()) Op(op);
    return index;
  }

  template <class Op>
  void Replace(Operation* replaced, const Op& with) {
    STATIC_ASSERT((std::is_base_of<Operation, Op>::value));
    STATIC_ASSERT(std::is_trivially_destructible<Op>::value);
    STATIC_ASSERT(sizeof(Op) <= kOperationSize);
    DCHECK(IsValid(Index(*replaced)));
    DCHECK(InputsValid(with));
    new (replaced) Op(with);
  }

  bool Add(Block* block) {
    if (!blocks_.empty() && block->predecessors.empty()) return false;
#ifdef DEBUG
    // Verify that the blocks are a pre-order of the dominator tree.
    Block* immediate_dominator = block->immediate_dominator;
    for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
      if (*it == immediate_dominator) break;
      DCHECK((*it)->IsDominatedBy(immediate_dominator));
    }
#endif
    DCHECK_EQ(block->first, OpIndex::kInvalid);
    block->first = next_operation_index();
    DCHECK_EQ(block->index, BlockIndex::kInvalid);
    block->index = static_cast<BlockIndex>(blocks_.size());
    blocks_.push_back(block);
    return true;
  }

  OpIndex next_operation_index() const {
    return static_cast<OpIndex>(operations_.size());
  }

  Zone* graph_zone() const { return graph_zone_; }
  size_t block_count() const { return blocks_.size(); }
  size_t op_count() const { return operations_.size(); }

  template <bool const_value>
  struct OperationIterator
      : base::iterator<std::bidirectional_iterator_tag,
                       base::add_const_if<const_value, Operation>> {
    base::add_const_if<const_value, OperationStorage>* ptr;
    using value_type = base::add_const_if<const_value, Operation>;

    explicit OperationIterator(
        base::add_const_if<const_value, OperationStorage>* ptr)
        : ptr(ptr) {}
    value_type& operator*() {
      return *reinterpret_cast<base::add_const_if<const_value, Operation>*>(
          ptr);
    }
    OperationIterator& operator++() {
      ++ptr;
      return *this;
    }
    OperationIterator& operator--() {
      --ptr;
      return *this;
    }
    bool operator!=(OperationIterator other) { return ptr != other.ptr; }
  };

  base::iterator_range<OperationIterator<true>> AllOperations() const {
    return {OperationIterator<true>(operations_.data()),
            OperationIterator<true>(operations_.data() + operations_.size())};
  }

  base::iterator_range<OperationIterator<false>> BlockIterator(
      const Block& block) {
    return {OperationIterator<false>(operations_.data() +
                                     ToUnderlyingType(block.first)),
            OperationIterator<false>(operations_.data() +
                                     ToUnderlyingType(block.last) + 1)};
  }
  base::iterator_range<OperationIterator<true>> BlockIterator(
      const Block& block) const {
    return {OperationIterator<true>(operations_.data() +
                                    ToUnderlyingType(block.first)),
            OperationIterator<true>(operations_.data() +
                                    ToUnderlyingType(block.last) + 1)};
  }

  base::iterator_range<Block* const*> blocks() {
    return {blocks_.data(), blocks_.data() + blocks_.size()};
  }
  base::iterator_range<const Block* const*> blocks() const {
    return {blocks_.data(), blocks_.data() + blocks_.size()};
  }

  bool IsValid(OpIndex i) const {
    return i < next_operation_index() && i != OpIndex::kInvalid;
  }

 private:
  bool InputsValid(const Operation& op) const {
    for (OpIndex i : op.Inputs()) {
      if (!IsValid(i)) return false;
    }
    return true;
  }

  ZoneVector<OperationStorage> operations_;
  ZoneVector<Block*> blocks_;
  Zone* graph_zone_;
};

// template <class Derived>
class Assembler {
 public:
  Block* NewBlock(Block::Kind kind) {
    Block* block = graph_zone()->template New<Block>(kind, graph_zone());
#ifdef DEBUG
    block->assembler = this;
#endif
    return block;
  }

  bool Bind(Block* block) {
    DCHECK_EQ(block->assembler, this);
    if (!graph().Add(block)) return false;
    DCHECK_NULL(current_block_);
    current_block_ = block;
    return true;
  }

  template <class Op>
  OpIndex Emit(const Op& op) {
    STATIC_ASSERT((std::is_base_of<Operation, Op>::value));
    STATIC_ASSERT(!(std::is_same<Op, Operation>::value));
    DCHECK_NOT_NULL(current_block_);
    OpIndex index = graph().Add(op);
    if (Op::properties.is_block_terminator) FinalizeBlock();
    return index;
  }

  explicit Assembler(Zone* graph_zone) : graph_(graph_zone) {}

  Block* current_block() { return current_block_; }
  Zone* graph_zone() { return graph().graph_zone(); }
  Graph& graph() { return graph_; }

 private:
  void FinalizeBlock() {
    DCHECK_EQ(current_block_->last, OpIndex::kInvalid);
    current_block_->last = graph().next_operation_index();
    current_block_ = nullptr;
  }

  Block* current_block_ = nullptr;
  Graph graph_;
};

class BasicAssembler : public Assembler {
 public:
  using Base = Assembler;
  using Base::Base;
  using Base::Emit;

  OpIndex Emit(const PhiOp& op) {
    DCHECK(current_block()->IsMerge() &&
           op.InputCount() == current_block()->predecessors.size());
    return Base::Emit(op);
  }

  OpIndex Emit(const BinaryPhiOp& op) {
    DCHECK(current_block()->IsMerge() &&
           op.InputCount() == current_block()->predecessors.size());
    return Base::Emit(op);
  }

  OpIndex Emit(const PendingLoopPhiOp& op) {
    DCHECK(current_block()->IsLoop());
    return Base::Emit(op);
  }

  OpIndex Emit(const LoopPhiOp& op) {
    DCHECK(current_block()->IsLoop() &&
           op.InputCount() == current_block()->predecessors.size());
    return Base::Emit(op);
  }

  OpIndex Emit(const GotoOp& op) {
    op.destination->AddPredecessor(current_block());
    DCHECK(current_block()->successors.empty());
    current_block()->successors = {op.destination};
    return Base::Emit(op);
  }

  OpIndex Emit(const BranchOp& op) {
    op.if_true()->AddPredecessor(current_block());
    op.if_false()->AddPredecessor(current_block());
    DCHECK(current_block()->successors.empty());
    current_block()->successors = {op.if_true(), op.if_false()};
    return Base::Emit(op);
  }
};

class Variable : public IntrusivePriorityQueue::Item {
  struct Assignment {
    Block* block;
    OpIndex value;
  };

 public:
  explicit Variable(VarAssembler* assembler);
  ~Variable();

  OpIndex LookupValue(Block* block) {
    for (auto it = assignments_.rbegin(); it != assignments_.rend(); ++it) {
      auto& assignment = *it;
      Block* assignment_block = assignment.block;
      if (assignment_block->dominator_depth > block->dominator_depth) continue;
      while (assignment_block->dominator_depth < block->dominator_depth) {
        // This relies on the block-ordering being a pre-order of the dominator
        // tree: Between a block and it's immediate dominator, there can only be
        // other blocks that share this dominator. Therefore, if we get to a
        // block with lower dominator_depth, we must already be past all the
        // blocks on our dominator path with higher dominator_depth.
        block = block->immediate_dominator;
      }
      if (block == assignment_block) return assignment.value;
    }
    return OpIndex::kInvalid;
  }

 private:
  friend class VarAssembler;
  VarAssembler* assembler_;
  ZoneVector<Assignment> assignments_;
};

class VarAssembler : public BasicAssembler {
 public:
  using Base = BasicAssembler;
  using Base::Base;
  using Base::Emit;

  void Write(Variable* variable, OpIndex value) {
    variable->assignments_.push_back({current_block(), value});
    variables_.AddOrUpdate(variable,
                           ToUnderlyingType(graph().next_operation_index()));
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
        if (value != OpIndex::kInvalid) {
          Write(var, Emit(PendingVariableLoopPhiOp(value, var)));
        }
      }
    } else if (block->IsMerge()) {
      if (block->predecessors.size() <= 1) return true;
      OpIndex dominating_pos = block->immediate_dominator->last;
      // Only look at the variables that changed since the block dominating the
      // merge. Otherwise, the old value from this block is still good and we
      // don't need a new phi.
      for (Variable* var :
           variables_.MinRange(ToUnderlyingType(dominating_pos))) {
        OpIndex first = var->LookupValue(block->predecessors[0]);
        bool needs_phi = false;
        bool is_valid = first != OpIndex::kInvalid;
        base::SmallVector<OpIndex, 8> inputs = {first};
        for (auto it = block->predecessors.begin() + 1;
             it != block->predecessors.end(); ++it) {
          OpIndex value = var->LookupValue(*it);
          needs_phi |= value != first;
          is_valid &= value != OpIndex::kInvalid;
          inputs.emplace_back(value);
        }
        if (needs_phi && is_valid) {
          Write(var, Emit(PhiOp(inputs.vector(), graph_zone())));
        }
      }
    }
    return true;
  }

  OpIndex Emit(const GotoOp& op) {
    if (op.destination->IsLoop()) {
      FixLoopPhis(op.destination, current_block());
    }
    return Base::Emit(op);
  }

  explicit VarAssembler(Zone* zone) : Base(zone), variables_(zone) {}

 private:
  void FixLoopPhis(Block* loop, Block* backedge) {
    DCHECK(loop->IsLoop());
    for (Operation& op : graph().BlockIterator(*loop)) {
      if (op.Is<PendingVariableLoopPhiOp>()) continue;
      auto& pending_phi = op.Cast<PendingVariableLoopPhiOp>();
      LoopPhiOp new_phi(pending_phi.first(),
                        pending_phi.variable->LookupValue(backedge),
                        backedge->index);
      Variable* var = pending_phi.variable;
      OpIndex first = pending_phi.first();
      OpIndex second = var->LookupValue(backedge);
      graph().Replace(&pending_phi, LoopPhiOp(first, second, backedge->index));
    }
  }

  friend class Variable;
  IntrusivePriorityQueueTempl<Variable> variables_;
};

inline Variable::Variable(VarAssembler* assembler)
    : assembler_(assembler), assignments_(assembler_->graph_zone()) {}
inline Variable::~Variable() { assembler_->variables_.Remove(this); }

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_CFG_H_
