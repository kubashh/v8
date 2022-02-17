// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_VALUE_NUMBERING_ASSEMBLER_H_
#define V8_COMPILER_TURBOSHAFT_VALUE_NUMBERING_ASSEMBLER_H_

#include <cstring>
#include <type_traits>

#include "src/base/bits.h"
#include "src/base/functional.h"
#include "src/base/vector.h"
#include "src/compiler/turboshaft/cfg.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace turboshaft {

class ValueNumberingAssembler
    : public AssemblerInterface<ValueNumberingAssembler, BasicAssembler> {
 private:
  struct Entry {
    OpIndex value;
    BlockIndex block;
    size_t hash;
  };

 public:
  ValueNumberingAssembler(Graph* graph, Zone* phase_zone)
      : AssemblerInterface(graph, phase_zone) {
    table_ = phase_zone->NewVector<Entry>(base::bits::RoundUpToPowerOfTwo(
        std::max<size_t>(128, graph->op_id_capacity() / 2)));
    table_.ZeroMemory();
    entry_count_ = 0;
    mask_ = table_.size() - 1;
  }

#define EMIT_OP(Name)                                    \
  template <class... Args>                               \
  OpIndex Name(Args... args) {                           \
    OpIndex next_index = graph().next_operation_index(); \
    USE(next_index);                                     \
    OpIndex result = Base::Name(args...);                \
    DCHECK_EQ(next_index, result);                       \
    return AddOrFind<Name##Op>(result);                  \
  }
  TURBOSHAFT_OPERATION_LIST(EMIT_OP)
#undef EMIT_OP

 private:
  template <class Op>
  OpIndex AddOrFind(OpIndex op_idx) {
    if (!Op::properties.is_pure || std::is_same<Op, PendingLoopPhiOp>::value ||
        std::is_same<Op, PendingVariableLoopPhiOp>::value) {
      return op_idx;
    }
    RehashIfNeeded();
    const Op& op = graph().Get(op_idx).Cast<Op>();
    constexpr bool same_block_only = std::is_same<Op, PhiOp>::value;
    size_t hash = ComputeHash<same_block_only>(op);
    size_t mask = mask_;
    // size_t chain_length = 0;
    // std::cout << "        inserting " << op << '\n';
    size_t start_index = hash & mask;
    for (size_t i = start_index;; i = (i + 1) & mask) {
      Entry& entry = table_[i];
      size_t entry_hash = entry.hash;
      // if (entry_hash != 0) std::cout << "            found " << entry.block
      // << " " << std::hex << entry.hash << std::dec << " " <<
      // graph().Get(entry.value) << '\n';
      if (entry_hash == 0) {
        entry = Entry{op_idx, current_block()->index, hash};
        ++entry_count_;
        // std::cout << "inserted after " << chain_length << " steps\n\n";
        return op_idx;
      } else if (entry_hash == hash) {
        const Operation& entry_op = graph().Get(entry.value);
        if (entry_op.Is<Op>() &&
            (!same_block_only || entry.block == current_block()->index)) {
          if (entry_op.Cast<Op>() == op &&
              (same_block_only ||
               current_block()->IsDominatedBy(&graph().Get(entry.block)))) {
            // std::cout << "found after " << chain_length << " steps\n\n";
            return entry.value;
          }
        }
      }
      // ++chain_length;
    }
  }

  void RehashIfNeeded() {
    if (table_.size() - (table_.size() / 4) > entry_count_) return;
    base::Vector<Entry> old_table = table_;
    base::Vector<Entry> new_table = table_ =
        phase_zone()->NewVector<Entry>(table_.size() * 2);
    new_table.ZeroMemory();
    size_t mask = mask_ = table_.size() - 1;

    for (const Entry& entry : old_table) {
      for (size_t i = entry.hash & mask;; i = (i + 1) & mask) {
        if (new_table[i].hash == 0) {
          new_table[i] = entry;
          break;
        }
      }
    }
  }

  template <bool same_block_only, class Op>
  size_t ComputeHash(const Op& op) {
    size_t hash = op.hash_value();
    if (same_block_only) {
      hash = base::hash_combine(current_block()->index, hash);
    }
    if (hash == 0) return 1;
    return hash;
  }

  base::Vector<Entry> table_;
  size_t mask_;
  size_t entry_count_;
};

}  // namespace turboshaft
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOSHAFT_VALUE_NUMBERING_ASSEMBLER_H_
