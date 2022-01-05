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
#include "src/compiler/turboshaft/instructions.h"
#include "src/compiler/turboshaft/intrusive-priority-queue.h"
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

  bool IsDominatedBy(Block* dominator) {
    Block* block = this;
    for (int i = dominator_depth - dominator->dominator_depth; i >= 0; --i) {
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
  InstrIndex first = InstrIndex::kInvalid;
  InstrIndex last = InstrIndex::kInvalid;
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
      : instructions_(graph_zone),
        blocks_(graph_zone),
        graph_zone_(graph_zone) {}

  const Instruction& Get(InstrIndex i) {
    DCHECK_LT(ToUnderlyingType(i), instructions_.size());
    return *reinterpret_cast<const Instruction*>(
        &instructions_[ToUnderlyingType(i)]);
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

  InstrIndex Index(const Instruction& instr) const {
    size_t i =
        reinterpret_cast<const decltype(instructions_)::value_type*>(&instr) -
        instructions_.data();
    DCHECK_LT(i, instructions_.size());
    return static_cast<InstrIndex>(i);
  }

  template <class Instr>
  InstrIndex Add(const Instr& instr) {
    DCHECK(InputsValid(instr));
    InstrIndex index = next_instruction_index();
    instructions_.emplace_back();
    new (&instructions_.back()) Instr(instr);
    return index;
  }

  template <class Instr>
  void Replace(Instruction* replaced, const Instr& with) {
    STATIC_ASSERT((std::is_base_of<Instruction, Instr>::value));
    STATIC_ASSERT(std::is_trivially_destructible<Instr>::value);
    STATIC_ASSERT(sizeof(Instr) <= kInstructionSize);
    DCHECK(ValidInstrIndex(Index(*replaced)));
    DCHECK(InputsValid(with));
    new (replaced) Instr(with);
  }

  void Add(Block* block) {
#ifdef DEBUG
    // Verify that the blocks are a pre-order of the dominator tree.
    Block* immediate_dominator = block->immediate_dominator;
    for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
      if (*it == immediate_dominator) break;
      DCHECK((*it)->IsDominatedBy(immediate_dominator));
    }
#endif
    DCHECK_EQ(block->first, InstrIndex::kInvalid);
    block->first = next_instruction_index();
    DCHECK_EQ(block->index, BlockIndex::kInvalid);
    block->index = static_cast<BlockIndex>(blocks_.size());
    blocks_.push_back(block);
  }

  InstrIndex next_instruction_index() {
    return static_cast<InstrIndex>(instructions_.size());
  }

  Zone* graph_zone() const { return graph_zone_; }
  size_t block_count() const { return blocks_.size(); }
  size_t instr_count() const { return instructions_.size(); }

  template <bool const_value>
  struct InstructionIterator
      : base::iterator<std::bidirectional_iterator_tag,
                       base::add_const_if<const_value, Instruction>> {
    base::add_const_if<const_value, InstructionStorage>* ptr;
    using value_type = base::add_const_if<const_value, Instruction>;

    explicit InstructionIterator(
        base::add_const_if<const_value, InstructionStorage>* ptr)
        : ptr(ptr) {}
    value_type& operator*() {
      return *reinterpret_cast<base::add_const_if<const_value, Instruction>*>(
          ptr);
    }
    void operator++() { ++ptr; }
    void operator--() { --ptr; }
    bool operator!=(InstructionIterator other) { return ptr != other.ptr; }
  };

  base::iterator_range<InstructionIterator<false>> BlockIterator(
      const Block& block) {
    return {InstructionIterator<false>(instructions_.data() +
                                       ToUnderlyingType(block.first)),
            InstructionIterator<false>(instructions_.data() +
                                       ToUnderlyingType(block.last) + 1)};
  }
  base::iterator_range<InstructionIterator<true>> BlockIterator(
      const Block& block) const {
    return {InstructionIterator<true>(instructions_.data() +
                                      ToUnderlyingType(block.last) + 1),
            InstructionIterator<true>(instructions_.data() +
                                      ToUnderlyingType(block.first))};
  }

  base::iterator_range<Block* const*> blocks() {
    return {blocks_.data(), blocks_.data() + blocks_.size()};
  }
  base::iterator_range<const Block* const*> blocks() const {
    return {blocks_.data(), blocks_.data() + blocks_.size()};
  }

 private:
  bool ValidInstrIndex(InstrIndex i) {
    return i < next_instruction_index() && i != InstrIndex::kInvalid;
  }
  bool InputsValid(const Instruction& instr) {
    for (InstrIndex i : instr.Inputs()) {
      if (!ValidInstrIndex(i)) return false;
    }
    return true;
  }

  ZoneVector<InstructionStorage> instructions_;
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

  void Bind(Block* block) {
    DCHECK_EQ(block->assembler, this);
    graph().Add(block);
    DCHECK_NULL(current_block_);
    current_block_ = block;
  }

  template <class Instr>
  InstrIndex Emit(const Instr& instr) {
    STATIC_ASSERT((std::is_base_of<Instruction, Instr>::value));
    STATIC_ASSERT(!(std::is_same<Instr, Instruction>::value));
    DCHECK_NOT_NULL(current_block_);
    InstrIndex index = graph().Add(instr);
    if (Instr::is_block_terminator) FinalizeBlock();
    return index;
  }

  explicit Assembler(Zone* graph_zone) : graph_(graph_zone) {}

  Block* current_block() { return current_block_; }
  Zone* graph_zone() { return graph().graph_zone(); }
  Graph& graph() { return graph_; }

 private:
  void FinalizeBlock() {
    DCHECK_EQ(current_block_->last, InstrIndex::kInvalid);
    current_block_->last = graph().next_instruction_index();
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

  InstrIndex Emit(const PhiInstr& instr) {
    DCHECK(current_block()->IsLoopOrMerge() &&
           instr.InputCount() == current_block()->predecessors.size());
    return Base::Emit(instr);
  }

  InstrIndex Emit(const GotoInstr& instr) {
    instr.destination->AddPredecessor(current_block());
    DCHECK(current_block()->successors.empty());
    current_block()->successors = {instr.destination};
    return Base::Emit(instr);
  }

  InstrIndex Emit(const BranchInstr& instr) {
    instr.if_true()->AddPredecessor(current_block());
    instr.if_false()->AddPredecessor(current_block());
    DCHECK(current_block()->successors.empty());
    current_block()->successors = {instr.if_true(), instr.if_false()};
    return Base::Emit(instr);
  }
};

class Variable : public IntrusivePriorityQueue::Item {
  struct Assignment {
    Block* block;
    InstrIndex value;
  };

 public:
  explicit Variable(VarAssembler* assembler);
  ~Variable();

  InstrIndex LookupValue(Block* block) {
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
    return InstrIndex::kInvalid;
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

  void Write(Variable* variable, InstrIndex value) {
    variable->assignments_.push_back({current_block(), value});
    variables_.AddOrUpdate(variable,
                           ToUnderlyingType(graph().next_instruction_index()));
  }

  InstrIndex Read(Variable* variable) {
    return variable->LookupValue(current_block());
  }

  void Bind(Block* block) {
    Base::Bind(block);
    if (block->IsLoop()) {
      DCHECK_EQ(block->predecessors.size(), 1);
      Block* predecessor = block->predecessors[0];
      for (Variable* var : variables_) {
        InstrIndex value = var->LookupValue(predecessor);
        if (value != InstrIndex::kInvalid) {
          Write(var, Emit(PendingVariableLoopPhiInstr(value, var)));
        }
      }
    } else if (block->IsMerge()) {
      if (block->predecessors.size() <= 1) return;
      InstrIndex dominating_pos = block->immediate_dominator->last;
      // Only look at the variables that changed since the block dominating the
      // merge. Otherwise, the old value from this block is still good and we
      // don't need a new phi.
      for (Variable* var :
           variables_.MinRange(ToUnderlyingType(dominating_pos))) {
        InstrIndex first = var->LookupValue(block->predecessors[0]);
        bool needs_phi = false;
        bool is_valid = first != InstrIndex::kInvalid;
        base::SmallVector<InstrIndex, 8> inputs = {first};
        for (auto it = block->predecessors.begin() + 1;
             it != block->predecessors.end(); ++it) {
          InstrIndex value = var->LookupValue(*it);
          needs_phi |= value != first;
          is_valid &= value != InstrIndex::kInvalid;
          inputs.emplace_back(value);
        }
        if (needs_phi && is_valid) {
          Write(var, Emit(PhiInstr(inputs.vector(), graph_zone())));
        }
      }
    }
  }

  InstrIndex Emit(const GotoInstr& instr) {
    if (instr.destination->IsLoop()) {
      FixLoopPhis(instr.destination, current_block());
    }
    return Base::Emit(instr);
  }

  explicit VarAssembler(Zone* zone) : Base(zone), variables_(zone) {}

 private:
  void FixLoopPhis(Block* loop, Block* backedge) {
    DCHECK(loop->IsLoop());
    for (Instruction& instr : graph().BlockIterator(*loop)) {
      if (instr.Is<PendingVariableLoopPhiInstr>()) continue;
      auto& pending_phi = instr.Cast<PendingVariableLoopPhiInstr>();
      LoopPhiInstr new_phi(pending_phi.first(),
                           pending_phi.variable->LookupValue(backedge),
                           backedge->index);
      Variable* var = pending_phi.variable;
      InstrIndex first = pending_phi.first();
      InstrIndex second = var->LookupValue(backedge);
      graph().Replace(&pending_phi,
                      LoopPhiInstr(first, second, backedge->index));
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
