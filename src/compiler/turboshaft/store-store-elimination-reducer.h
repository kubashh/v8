// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_STORE_STORE_ELIMINATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_STORE_STORE_ELIMINATION_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/compiler/turboshaft/uniform-reducer-adapter.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

struct KeyData {
  OpIndex base;
  int32_t offset;
  uint8_t size;
};

enum class StoreObservability {
  kUnobservable = 0,
  kGCObservable = 1,
  kObservable = 2,
};

inline std::ostream& operator<<(std::ostream& os,
                                StoreObservability observability) {
  switch (observability) {
    case StoreObservability::kUnobservable:
      return os << "Unobservable";
    case StoreObservability::kGCObservable:
      return os << "GCObservable";
    case StoreObservability::kObservable:
      return os << "Observable";
  }
}

class MaybeRedundantStoresTable
    : public ChangeTrackingSnapshotTable<MaybeRedundantStoresTable,
                                         StoreObservability, KeyData> {
  using super = ChangeTrackingSnapshotTable<MaybeRedundantStoresTable,
                                            StoreObservability, KeyData>;

 public:
  explicit MaybeRedundantStoresTable(const Graph& graph, Zone* zone)
      : ChangeTrackingSnapshotTable(zone),
        graph_(graph),
        block_to_snapshot_mapping_(zone),
        key_mapping_(zone),
        active_keys_(zone),
        successor_snapshots_(zone) {}

  void OnNewKey(Key key, StoreObservability value) {
    DCHECK_EQ(value, StoreObservability::kObservable);
    DCHECK(!active_keys_.contains(key));
  }

  void OnValueChange(Key key, StoreObservability old_value,
                     StoreObservability new_value) {
    DCHECK_NE(old_value, new_value);
    if (new_value != StoreObservability::kObservable) {
      active_keys_.insert(key);
    } else if (auto it = active_keys_.find(key); it != active_keys_.end()) {
      active_keys_.erase(it);
    }
    // Otherwise the key has already been erased by the caller.
  }

  void BeginBlock(const Block* block, bool is_loop_revisit) {
    // Seal the current block first.
    if (IsSealed()) {
      DCHECK_NULL(current_block_);
    } else {
      // If we bind a new block while the previous one is still unsealed, we
      // finalize it.
      Seal();
    }

    // Collect the snapshots of all successors.
    {
      auto successors = SuccessorBlocks(block->LastOperation(graph_));
      successor_snapshots_.clear();
      for (const Block* s : successors) {
        if (s->IsLoop() || !is_loop_revisit) continue;
        base::Optional<Snapshot> s_snapshot =
            block_to_snapshot_mapping_[s->index()];
        DCHECK(s_snapshot.has_value());
        successor_snapshots_.push_back(*s_snapshot);
      }
    }

    // Start a new snapshot for this block by merging information from
    // predecessors.
    StartNewSnapshot(
        base::VectorOf(successor_snapshots_),
        [](Key, base::Vector<const StoreObservability> successors) {
          return *std::max_element(successors.begin(), successors.end());
        });

    current_block_ = block;
  }

  StoreObservability GetObservability(OpIndex base, int32_t offset,
                                      uint8_t size) {
    Key key = map_to_key(base, offset, size);
    if (key.data().size < size) return StoreObservability::kObservable;
    return Get(key);
  }

  void MarkStoreAsUnobservable(OpIndex base, int32_t offset, uint8_t size) {
    // We can only shadow stores to the exact same `base`+`offset` and keep
    // everything else because they might or might not alias.
    Key key = map_to_key(base, offset, size);
    DCHECK_LE(key.data().size, size);
    Set(key, StoreObservability::kUnobservable);
  }

  void MarkPotentiallyAliasingStoresAsObservable(OpIndex base, int32_t offset) {
    // For now, we consider all stores to the same offset as potentially
    // aliasing. We might improve this to eliminate more precisely, if we have
    // some sort of aliasing information.
    std::vector<Key> active_keys_with_offset;
    std::copy_if(active_keys_.begin(), active_keys_.end(),
                 std::back_inserter(active_keys_with_offset),
                 [offset](const Key& k) { return k.data().offset == offset; });
    for (Key key : active_keys_with_offset) {
      Set(key, StoreObservability::kObservable);
    }
  }

  void MarkAllStoresAsObservable() {
    auto active_keys = std::move(active_keys_);
    for (Key key : active_keys) {
      Set(key, StoreObservability::kObservable);
    }
  }

  void MarkAllStoresAsGCObservable() {
    std::vector<Key> active_keys(active_keys_.begin(), active_keys_.end());
    for (Key key : active_keys) {
      auto current = Get(key);
      DCHECK_NE(current, StoreObservability::kObservable);
      if (current == StoreObservability::kUnobservable) {
        Set(key, StoreObservability::kGCObservable);
      }
    }
  }

  void Seal(bool* snapshot_has_changed = nullptr) {
    DCHECK(!IsSealed());
    DCHECK_NOT_NULL(current_block_);
    DCHECK(current_block_->index().valid());
    auto& snapshot = block_to_snapshot_mapping_[current_block_->index()];
    base::Optional<Snapshot> old_snapshot = snapshot;
    snapshot = super::Seal();
    current_block_ = nullptr;

    if (snapshot_has_changed) {
      if (!old_snapshot.has_value()) {
        *snapshot_has_changed = true;
      } else {
        StartNewSnapshot(
            base::VectorOf({old_snapshot.value(), snapshot.value()}),
            [&](Key key, base::Vector<const StoreObservability> successors) {
              DCHECK_LE(successors[0], successors[1]);
              if (successors[0] != successors[1]) *snapshot_has_changed = true;
              return successors[1];
            });
        super::Seal();
      }
    }
  }

  void Print(std::ostream& os, const char* sep = "\n") const {
    bool first = true;
    for (Key key : active_keys_) {
      os << (first ? "" : sep) << key.data().base.id() << "@"
         << key.data().offset << ": " << Get(key);
      first = false;
    }
  }

 private:
  Key map_to_key(OpIndex base, int32_t offset, uint8_t size) {
    std::pair p{base, offset};
    auto it = key_mapping_.find(p);
    if (it != key_mapping_.end()) return it->second;
    Key new_key =
        NewKey(KeyData{base, offset, size}, StoreObservability::kObservable);
    key_mapping_.emplace(p, new_key);
    return new_key;
  }

  const Graph& graph_;
  GrowingBlockSidetable<base::Optional<Snapshot>> block_to_snapshot_mapping_;
  ZoneUnorderedMap<std::pair<OpIndex, int32_t>, Key> key_mapping_;
  ZoneUnorderedSet<Key> active_keys_;
  const Block* current_block_ = nullptr;
  ZoneVector<Snapshot> successor_snapshots_;
};

class RedundantStoreAnalysis {
 public:
  RedundantStoreAnalysis(const Graph& graph, Zone* phase_zone)
      : graph_(graph), table_(graph, phase_zone) {}

  void Run(ZoneSet<OpIndex>& eliminable_stores) {
    eliminable_stores_ = &eliminable_stores;
    for (uint32_t processed = graph_.block_count(); processed > 0;
         --processed) {
      BlockIndex block_index = static_cast<BlockIndex>(processed - 1);

      const Block& block = graph_.Get(block_index);
      ProcessBlock(block, &processed);

      // If this block is a loop header, check if this loop needs to be
      // revisited.
      if (block.IsLoop()) {
        DCHECK(!table_.IsSealed());
        bool needs_revisit = false;
        table_.Seal(&needs_revisit);
        if (needs_revisit) {
          Block* back_edge = block.LastPredecessor();
          DCHECK_GT(back_edge->index(), block_index);
          processed = back_edge->index().id() + 1;
        }
      }
    }
    eliminable_stores_ = nullptr;
  }

  void ProcessBlock(const Block& block, uint32_t* processed) {
    table_.BeginBlock(&block, false);

    auto op_range = graph_.OperationIndices(block);
    for (auto it = op_range.end(); it != op_range.begin();) {
      --it;
      OpIndex index = *it;
      const Operation& op = graph_.Get(index);

      switch (op.opcode) {
        case Opcode::kStore: {
          const StoreOp& store = op.Cast<StoreOp>();
          // TODO(nicohartmann@): Use the new effect flags to distinguish heap
          // access once available.
          const bool is_on_heap_store = store.kind.tagged_base;
          const bool is_field_store = !store.index().valid();
          const uint8_t size = store.stored_rep.SizeInBytes();
          // For now we consider only stores of fields of objects on the heap.
          if (is_on_heap_store && is_field_store) {
            switch (table_.GetObservability(store.base(), store.offset, size)) {
              case StoreObservability::kUnobservable:
                eliminable_stores_->insert(index);
                break;
              case StoreObservability::kGCObservable:
                if (store.maybe_initializing_or_transitioning) {
                  // We keep this store.
                  table_.MarkStoreAsUnobservable(store.base(), store.offset,
                                                 size);
                } else {
                  eliminable_stores_->insert(index);
                }
                break;
              case StoreObservability::kObservable:
                // We keep this store.
                table_.MarkStoreAsUnobservable(store.base(), store.offset,
                                               size);
                break;
            }
          }
          break;
        }
        case Opcode::kLoad: {
          const LoadOp& load = op.Cast<LoadOp>();
          // TODO(nicohartmann@): Use the new effect flags to distinguish heap
          // access once available.
          const bool is_on_heap_load = load.kind.tagged_base;
          const bool is_field_load = !load.index().valid();
          // For now we consider only loads of fields of objects on the heap.
          if (is_on_heap_load && is_field_load) {
            table_.MarkPotentiallyAliasingStoresAsObservable(load.base(),
                                                             load.offset);
          }
          break;
        }
        case Opcode::kAllocate: {
          table_.MarkAllStoresAsGCObservable();
          break;
        }
        default:
          if (MayObserveStoreField(op)) {
            table_.MarkAllStoresAsObservable();
          }
          break;
      }
    }
  }

  bool MayObserveStoreField(const Operation& op) {
    const auto& props = op.Properties();
    if (props.is_pure_no_allocation) return false;
    // TODO(nicohartmann): Extend this.
    return true;
  }

 private:
  const Graph& graph_;
  MaybeRedundantStoresTable table_;
  ZoneSet<OpIndex>* eliminable_stores_ = nullptr;
};

template <class Next>
class StoreStoreEliminationReducer
    : public UniformReducerAdapter<StoreStoreEliminationReducer, Next> {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()
  using Adapter = UniformReducerAdapter<StoreStoreEliminationReducer, Next>;

  void Analyze() {
    analysis_.Run(eliminable_stores_);
    Adapter::Analyze();
  }

  OpIndex REDUCE_INPUT_GRAPH(Store)(OpIndex ig_index, const StoreOp& store) {
    if (eliminable_stores_.contains(ig_index)) {
      return OpIndex::Invalid();
    }
    return Next::ReduceInputGraphStore(ig_index, store);
  }

 private:
  RedundantStoreAnalysis analysis_{Asm().input_graph(), Asm().phase_zone()};
  ZoneSet<OpIndex> eliminable_stores_{Asm().phase_zone()};
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_STORE_STORE_ELIMINATION_REDUCER_H_
