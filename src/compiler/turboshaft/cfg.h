// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_CFG_H_
#define V8_COMPILER_TURBOSHAFT_CFG_H_

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
#include "src/compiler/turboshaft/intrusive-priority-queue.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

class Assembler;
class VarAssembler;

class OperationBuffer {
 public:
  class ReplaceScope {
   public:
    ReplaceScope(OperationBuffer* buffer, OpIndex replaced)
        : buffer_(buffer),
          replaced_(replaced),
          old_end_(buffer->end_),
          old_slot_count_(buffer->SlotCount(replaced)) {
      buffer_->end_ = buffer_->Get(replaced);
    }
    ~ReplaceScope() {
      DCHECK_LE(buffer_->SlotCount(replaced_), old_slot_count_);
      buffer_->end_ = old_end_;
      // Preserve the original operation size in case it has become smaller.
      buffer_->operation_sizes_[replaced_.id()] = old_slot_count_;
      buffer_->operation_sizes_[OpIndex(replaced_.offset() +
                                        static_cast<uint32_t>(old_slot_count_) *
                                            sizeof(OperationStorageSlot))
                                    .id() -
                                1] = old_slot_count_;
    }

    ReplaceScope(const ReplaceScope&) = delete;
    ReplaceScope& operator=(const ReplaceScope&) = delete;

   private:
    OperationBuffer* buffer_;
    OpIndex replaced_;
    OperationStorageSlot* old_end_;
    uint16_t old_slot_count_;
  };

  explicit OperationBuffer(Zone* zone, size_t initial_capacity) : zone_(zone) {
    begin_ = end_ = zone_->NewArray<OperationStorageSlot>(initial_capacity);
    operation_sizes_ =
        zone_->NewArray<uint16_t>((initial_capacity + 1) / kSlotsPerId);
    end_cap_ = begin_ + initial_capacity;
  }

  OperationStorageSlot* Allocate(size_t slot_count) {
    if (V8_UNLIKELY(static_cast<size_t>(end_cap_ - end_) < slot_count)) {
      Grow(capacity() + slot_count);
      DCHECK(slot_count <= static_cast<size_t>(end_cap_ - end_));
    }
    OperationStorageSlot* result = end_;
    end_ += slot_count;
    OpIndex idx = Index(result);
    // Store the size in both for the first and last id corresponding to the new
    // operation. The two id's are the same if the operation is small.
    operation_sizes_[idx.id()] = slot_count;
    operation_sizes_[OpIndex(idx.offset() + static_cast<uint32_t>(slot_count) *
                                                sizeof(OperationStorageSlot))
                         .id() -
                     1] = slot_count;
    return result;
  }

  OpIndex Index(const Operation& op) const {
    return Index(reinterpret_cast<const OperationStorageSlot*>(&op));
  }
  OpIndex Index(const OperationStorageSlot* ptr) const {
    DCHECK(begin_ <= ptr && ptr <= end_);
    return OpIndex(static_cast<uint32_t>(reinterpret_cast<const char*>(ptr) -
                                         reinterpret_cast<char*>(begin_)));
  }

  OperationStorageSlot* Get(OpIndex idx) {
    DCHECK_LT(idx.offset() / sizeof(OperationStorageSlot), size());
    return reinterpret_cast<OperationStorageSlot*>(
        reinterpret_cast<char*>(begin_) + idx.offset());
  }
  uint16_t SlotCount(OpIndex idx) {
    DCHECK_LT(idx.offset() / sizeof(OperationStorageSlot), size());
    return operation_sizes_[idx.id()];
  }

  const OperationStorageSlot* Get(OpIndex idx) const {
    DCHECK_LT(idx.offset(), capacity() * sizeof(OperationStorageSlot));
    return reinterpret_cast<const OperationStorageSlot*>(
        reinterpret_cast<const char*>(begin_) + idx.offset());
  }

  OpIndex Next(OpIndex idx) const {
    DCHECK_GT(operation_sizes_[idx.id()], 0);
    OpIndex result = OpIndex(idx.offset() + operation_sizes_[idx.id()] *
                                                sizeof(OperationStorageSlot));
    DCHECK_LE(result.offset(), capacity() * sizeof(OperationStorageSlot));
    return result;
  }
  OpIndex Previous(OpIndex idx) const {
    DCHECK_GT(operation_sizes_[idx.id() - 1], 0);
    OpIndex result = OpIndex(idx.offset() - operation_sizes_[idx.id() - 1] *
                                                sizeof(OperationStorageSlot));
    DCHECK_LT(result.offset(), capacity() * sizeof(OperationStorageSlot));
    return result;
  }

  OpIndex BeginIndex() const { return OpIndex(0); }
  OpIndex EndIndex() const { return Index(end_); }

  size_t size() const { return static_cast<size_t>(end_ - begin_); }
  size_t capacity() const { return static_cast<size_t>(end_cap_ - begin_); }

  void Grow(size_t min_capacity) {
    size_t size = this->size();
    size_t capacity = this->capacity();
    size_t new_capacity = 2 * capacity;
    while (new_capacity < min_capacity) new_capacity *= 2;
    CHECK_LT(new_capacity, std::numeric_limits<uint32_t>::max() /
                               sizeof(OperationStorageSlot));

    OperationStorageSlot* new_buffer =
        zone_->NewArray<OperationStorageSlot>(new_capacity);
    memcpy(new_buffer, begin_, size * sizeof(OperationStorageSlot));

    uint16_t* new_operation_sizes =
        zone_->NewArray<uint16_t>(new_capacity / kSlotsPerId);
    memcpy(new_operation_sizes, operation_sizes_,
           size / kSlotsPerId * sizeof(uint16_t));

    begin_ = new_buffer;
    end_ = new_buffer + size;
    end_cap_ = new_buffer + new_capacity;
    operation_sizes_ = new_operation_sizes;
  }

  void Reset() { end_ = begin_; }

 private:
  Zone* zone_;
  OperationStorageSlot* begin_;
  OperationStorageSlot* end_;
  OperationStorageSlot* end_cap_;
  uint16_t* operation_sizes_;
};

struct Block {
  enum class Kind : uint8_t { kMerge, kLoop, kBranchTarget };

  Block(Kind kind, Zone* graph_zone) : kind(kind) {}

  void Reset(Kind kind) {
    this->kind = kind;
    deferred = false;
    begin = OpIndex::Invalid();
    end = OpIndex::Invalid();
    index = BlockIndex::kInvalid;
    immediate_dominator = nullptr;
    dominator_depth = 0;
    predecessors.clear();
    successors.clear();
  }

  bool IsLoopOrMerge() const { return IsLoop() || IsMerge(); }
  bool IsLoop() const { return kind == Kind::kLoop; }
  bool IsMerge() const { return kind == Kind::kMerge; }
  bool IsHandler() const { return false; }
  bool IsSwitchCase() const { return false; }

  bool IsDeferred() const { return false; }

  bool Contains(OpIndex op_idx) const {
    return begin <= op_idx && op_idx < end;
  }

  const Block* LoopHeader() const {
    const Block* result = this;
    while (true) {
      if (result == nullptr || result->IsLoop()) return result;
      result = result->ImmediateDominator();
    }
  }
  const Block* LoopEnd() const {
    DCHECK(IsLoop());
    DCHECK_EQ(predecessors.size(), 2);
    return predecessors[1];
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
        case Kind::kBranchTarget:
          UNREACHABLE();
      }
    }
    predecessors.push_back(predecessor);
  }

  const Block* ImmediateDominator() const { return immediate_dominator; }

  Kind kind;
  bool deferred = false;
  OpIndex begin = OpIndex::Invalid();
  OpIndex end = OpIndex::Invalid();
  BlockIndex index = BlockIndex::kInvalid;
  Block* immediate_dominator = nullptr;
  int dominator_depth = 0;
  base::SmallVector<Block*, 2> predecessors;
  base::SmallVector<Block*, 2> successors;
#ifdef DEBUG
  Graph* graph = nullptr;
#endif
};

class Graph {
 public:
  explicit Graph(Zone* graph_zone, size_t initial_capacity = 2048)
      : operations_(graph_zone, initial_capacity),
        bound_blocks_(graph_zone),
        all_blocks_(graph_zone),
        graph_zone_(graph_zone) {}

  void Reset() {
    operations_.Reset();
    bound_blocks_.clear();
    next_block_ = 0;
  }

  const Operation& Get(OpIndex i) const {
    const Operation* ptr =
        reinterpret_cast<const Operation*>(operations_.Get(i));
    // Detect invalid memory by checking if opcode is valid.
    DCHECK_LT(ToUnderlyingType(ptr->opcode), kNumberOfOpcodes);
    return *ptr;
  }
  Operation& Get(OpIndex i) {
    Operation* ptr = reinterpret_cast<Operation*>(operations_.Get(i));
    // Detect invalid memory by checking if opcode is valid.
    DCHECK_LT(ToUnderlyingType(ptr->opcode), kNumberOfOpcodes);
    return *ptr;
  }

  const Block& StartBlock() const { return Get(static_cast<BlockIndex>(0)); }

  Block& Get(BlockIndex i) {
    DCHECK_LT(ToUnderlyingType(i), bound_blocks_.size());
    return *bound_blocks_[ToUnderlyingType(i)];
  }
  const Block& Get(BlockIndex i) const {
    DCHECK_LT(ToUnderlyingType(i), bound_blocks_.size());
    return *bound_blocks_[ToUnderlyingType(i)];
  }

  OpIndex Index(const Operation& op) const { return operations_.Index(op); }

  OperationStorageSlot* Allocate(size_t slot_count) {
    return operations_.Allocate(slot_count);
  }

  template <class Op, class... Args>
  OpIndex Add(Args... args) {
    OpIndex result = next_operation_index();
    Op& op = Op::New(this, args...);
    USE(op);
    DCHECK_EQ(result, Index(op));
    // std::cout << "\n" << result.id() << ": " << Get(result);
    return result;
  }

  template <class Op, class... Args>
  void Replace(OpIndex replaced, Args... args) {
    STATIC_ASSERT((std::is_base_of<Operation, Op>::value));
    STATIC_ASSERT(std::is_trivially_destructible<Op>::value);

    OperationBuffer::ReplaceScope replace_scope(&operations_, replaced);
    Op::New(this, args...);
  }

  Block* NewBlock(Block::Kind kind) {
    if (next_block_ < all_blocks_.size()) {
      Block* result = &all_blocks_[next_block_++];
      result->Reset(kind);
      return result;
    } else {
      all_blocks_.emplace_back(kind, graph_zone_);
      next_block_++;
      Block* result = &all_blocks_.back();
#ifdef DEBUG
      result->graph = this;
#endif
      return result;
    }
  }

  bool Add(Block* block) {
    if (!bound_blocks_.empty() && block->predecessors.empty()) return false;
    block->deferred = true;
    for (Block* pred : block->predecessors) {
      if (!pred->deferred) {
        block->deferred = false;
        break;
      }
    }
    DCHECK(!block->begin.valid());
    block->begin = next_operation_index();
    DCHECK_EQ(block->index, BlockIndex::kInvalid);
    block->index = static_cast<BlockIndex>(bound_blocks_.size());
    bound_blocks_.push_back(block);
    return true;
  }

  // For compatibility with the TurboFan backend, add a dummy end-block.
  void AddEndBlock() {
    Block* end_block = NewBlock(Block::Kind::kMerge);
    for (Block* block : bound_blocks_) {
      if (block->successors.empty()) {
        block->successors = base::make_array(end_block);
        end_block->AddPredecessor(block);
      }
    }
    Add(end_block);
    end_block->end = next_operation_index();
  }

  OpIndex next_operation_index() const { return operations_.EndIndex(); }

  Zone* graph_zone() const { return graph_zone_; }
  size_t block_count() const { return bound_blocks_.size(); }
  size_t op_id_count() const {
    return (operations_.size() + (kSlotsPerId - 1)) / kSlotsPerId;
  }

  template <bool const_value>
  struct OperationIterator
      : base::iterator<std::bidirectional_iterator_tag,
                       base::add_const_if<const_value, Operation>> {
    OpIndex idx;
    base::add_const_if<const_value, Graph>* const graph;
    using value_type = base::add_const_if<const_value, Operation>;

    explicit OperationIterator(OpIndex idx,
                               base::add_const_if<const_value, Graph>* graph)
        : idx(idx), graph(graph) {}
    value_type& operator*() { return graph->Get(idx); }
    OperationIterator& operator++() {
      idx = graph->operations_.Next(idx);
      return *this;
    }
    OperationIterator& operator--() {
      idx = graph->operations_.Previous(idx);
      return *this;
    }
    bool operator!=(OperationIterator other) { return idx != other.idx; }
  };

  template <class T>
  struct DerefPtrIterator : base::iterator<std::bidirectional_iterator_tag, T> {
    T* const* ptr;

    explicit DerefPtrIterator(T* const* ptr) : ptr(ptr) {}

    T& operator*() { return **ptr; }
    DerefPtrIterator& operator++() {
      ++ptr;
      return *this;
    }
    DerefPtrIterator& operator--() {
      --ptr;
      return *this;
    }
    bool operator!=(DerefPtrIterator other) { return ptr != other.ptr; }
  };

  base::iterator_range<OperationIterator<false>> AllOperations() {
    return {OperationIterator<false>(operations_.BeginIndex(), this),
            OperationIterator<false>(operations_.EndIndex(), this)};
  }

  base::iterator_range<OperationIterator<true>> AllOperations() const {
    return {OperationIterator<true>(operations_.BeginIndex(), this),
            OperationIterator<true>(operations_.EndIndex(), this)};
  }

  base::iterator_range<OperationIterator<false>> operations(
      const Block& block) {
    return {OperationIterator<false>(block.begin, this),
            OperationIterator<false>(block.end, this)};
  }
  base::iterator_range<OperationIterator<true>> operations(
      const Block& block) const {
    return {OperationIterator<true>(block.begin, this),
            OperationIterator<true>(block.end, this)};
  }

  base::iterator_range<DerefPtrIterator<Block>> blocks() {
    return {
        DerefPtrIterator<Block>(bound_blocks_.data()),
        DerefPtrIterator<Block>(bound_blocks_.data() + bound_blocks_.size())};
  }
  base::iterator_range<DerefPtrIterator<const Block>> blocks() const {
    return {DerefPtrIterator<const Block>(bound_blocks_.data()),
            DerefPtrIterator<const Block>(bound_blocks_.data() +
                                          bound_blocks_.size())};
  }

  bool IsValid(OpIndex i) const { return i < next_operation_index(); }

  Graph& GetOrCreateCompanion() {
    if (!companion_) {
      companion_ = std::make_unique<Graph>(graph_zone_, operations_.size());
    }
    return *companion_;
  }

  void SwapWithCompanion() {
    Graph& companion = GetOrCreateCompanion();
    std::swap(operations_, companion.operations_);
    std::swap(bound_blocks_, companion.bound_blocks_);
    std::swap(all_blocks_, companion.all_blocks_);
    std::swap(next_block_, companion.next_block_);
    std::swap(graph_zone_, companion.graph_zone_);
  }

 private:
  bool InputsValid(const Operation& op) const {
    for (OpIndex i : op.inputs()) {
      if (!IsValid(i)) return false;
    }
    return true;
  }

  OperationBuffer operations_;
  ZoneVector<Block*> bound_blocks_;
  ZoneDeque<Block> all_blocks_;
  size_t next_block_ = 0;
  Zone* graph_zone_;
  std::unique_ptr<Graph> companion_ = {};
};

std::ostream& operator<<(std::ostream& os, const Block& block);
std::ostream& operator<<(std::ostream& os, const Graph& graph);

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

  explicit Assembler(Graph* graph) : graph_(*graph) { graph_.Reset(); }

  Block* current_block() { return current_block_; }
  Zone* graph_zone() { return graph().graph_zone(); }
  Graph& graph() { return graph_; }

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
};

inline OperationStorageSlot* AllocateOpStorage(Graph* graph,
                                               size_t slot_count) {
  return graph->Allocate(slot_count);
}

class BasicAssembler : public Assembler {
 public:
  using Base = Assembler;
  using Base::Base;

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

  explicit VarAssembler(Graph* graph)
      : Base(graph), variables_(graph->graph_zone()) {}

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

#endif  // V8_COMPILER_TURBOSHAFT_CFG_H_
