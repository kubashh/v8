// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_GRAPH_PROCESSOR_H_
#define V8_MAGLEV_MAGLEV_GRAPH_PROCESSOR_H_

#include "src/compiler/bytecode-analysis.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

// The GraphProcessor takes a NodeProcessor, and applies it to each Node in the
// Graph by calling NodeProcessor::Process on each Node.
//
// The GraphProcessor also keeps track of the current ProcessingState, and
// passes this to the Process method.
//
// It expects a NodeProcessor class with:
//
//   // A function that processes the graph before the nodes are walked.
//   void PreProcessGraph(Graph* graph);
//
//   // A function that processes the graph after the nodes are walked.
//   void PostProcessGraph(Graph* graph);
//
//   // A function that processes each basic block before its nodes are walked.
//   void PreProcessBasicBlock(BasicBlock* block);
//
//   // Process methods for each Node type. The GraphProcessor switches over
//   // the Node's opcode, casts it to the appropriate FooNode, and dispatches
//   // to NodeProcessor::Process. It's then up to the NodeProcessor to provide
//   // either distinct Process methods per Node type, or using templates or
//   // overloading as appropriate to group node processing.
//   void Process(FooNode* node, const ProcessingState& state) {}
//
template <typename NodeProcessor, bool visit_identity_nodes = false>
class GraphProcessor;

enum class ProcessResult {
  kContinue,  // Process exited normally, and the following processors will be
              // called on the node.
  kRemove     // Remove the current node from the graph (and do not call the
              // following processors).
};

class ProcessingState {
 public:
  explicit ProcessingState(BlockConstIterator block_it,
                           NodeIterator* node_it = nullptr)
      : block_it_(block_it), node_it_(node_it) {}

  // Disallow copies, since the underlying frame states stay mutable.
  ProcessingState(const ProcessingState&) = delete;
  ProcessingState& operator=(const ProcessingState&) = delete;

  BasicBlock* block() const { return *block_it_; }
  BasicBlock* next_block() const { return *(block_it_ + 1); }

  NodeIterator* node_it() const {
    DCHECK_NOT_NULL(node_it_);
    return node_it_;
  }

 private:
  BlockConstIterator block_it_;
  NodeIterator* node_it_;
};

template <typename NodeProcessor, bool visit_identity_nodes>
class GraphProcessor {
 public:
  template <typename... Args>
  explicit GraphProcessor(Args&&... args)
      : node_processor_(std::forward<Args>(args)...) {}

  void ProcessBlock(BasicBlock* block) {
    node_processor_.PreProcessBasicBlock(block);

    if (block->has_phi()) {
      auto& phis = *block->phis();
      for (auto it = phis.begin(); it != phis.end();) {
        Phi* phi = *it;
        ProcessResult result = node_processor_.Process(phi, GetCurrentState());
        if (V8_UNLIKELY(result == ProcessResult::kRemove)) {
          it = phis.RemoveAt(it);
        } else {
          ++it;
        }
      }
    }

    for (node_it_ = block->nodes().begin(); node_it_ != block->nodes().end();) {
      Node* node = *node_it_;
      ProcessResult result = ProcessNodeBase(node, GetCurrentState());
      if (V8_UNLIKELY(result == ProcessResult::kRemove)) {
        node_it_ = block->nodes().RemoveAt(node_it_);
      } else {
        ++node_it_;
      }
    }

    ProcessNodeBase(block->control_node(), GetCurrentState());
  }

  void ComputeDeferred(Graph* graph, ZoneVector<BasicBlock*>& deferred,
                       ZoneVector<BasicBlock*>& non_deferred) {
    // Propagate deferredness: If a block is deferred, defer all its successors,
    // except if a successor has another predecessor which is not deferred.
    ZoneVector<BasicBlock*> work_queue(graph->zone());
    for (block_it_ = graph->begin(); block_it_ != graph->end(); ++block_it_) {
      BasicBlock* block = *block_it_;
      if (block->is_deferred()) {
        work_queue.emplace_back(block);
      }
    }

    // The algorithm below is O(e^2) where e is the maximum number of
    // predecessors. We check whether we should defer a block at most e times
    // where e is the number of predecessors. When doing the check, we check
    // each predecessor once.
    for (size_t i = 0; i < work_queue.size(); ++i) {
      BasicBlock* block = work_queue[i];
      DCHECK(block->is_deferred());
      for (BasicBlock* successor : block->successors()) {
        if (successor->is_deferred()) {
          continue;
        }
        if (successor->has_state()) {
          int predecessor_count = successor->predecessor_count();
          bool should_defer = true;
          for (int p = 0; p < predecessor_count; ++p) {
            if (!successor->predecessor_at(p)->is_deferred()) {
              should_defer = false;
              break;
            }
          }
          if (should_defer) {
            work_queue.emplace_back(successor);
            successor->set_deferred(true);
          }
        } else {
          DCHECK_EQ(successor->predecessor(), block);
          work_queue.emplace_back(successor);
          successor->set_deferred(true);
        }
      }
    }

    for (block_it_ = graph->begin(); block_it_ != graph->end(); ++block_it_) {
      BasicBlock* block = *block_it_;
      if (block->is_deferred()) {
        deferred.emplace_back(block);
      } else {
        non_deferred.emplace_back(block);
      }
    }
  }

  void ProcessGraph(Graph* graph) {
    graph_ = graph;

    node_processor_.PreProcessGraph(graph);

    auto process_constants = [&](auto& map) {
      for (auto it = map.begin(); it != map.end();) {
        ProcessResult result =
            node_processor_.Process(it->second, GetCurrentState());
        if (V8_UNLIKELY(result == ProcessResult::kRemove)) {
          it = map.erase(it);
        } else {
          ++it;
        }
      }
    };
    process_constants(graph->constants());
    process_constants(graph->root());
    process_constants(graph->smi());
    process_constants(graph->tagged_index());
    process_constants(graph->int32());
    process_constants(graph->uint32());
    process_constants(graph->float64());
    process_constants(graph->external_references());

    if (defer_deferred_blocks_) {
      // Divide blocks into deferred and non-deferred lists; this is the easiest
      // way to keep track of what the next block is.
      ZoneVector<BasicBlock*> non_deferred(graph->zone());
      ZoneVector<BasicBlock*> deferred(graph->zone());
      ComputeDeferred(graph, deferred, non_deferred);

      for (block_it_ = non_deferred.begin(); block_it_ != non_deferred.end();
           ++block_it_) {
        BasicBlock* block = *block_it_;
        ProcessBlock(block);
      }

      for (block_it_ = deferred.begin(); block_it_ != deferred.end();
           ++block_it_) {
        BasicBlock* block = *block_it_;
        ProcessBlock(block);
      }

    } else {
      for (block_it_ = graph->begin(); block_it_ != graph->end(); ++block_it_) {
        BasicBlock* block = *block_it_;

        ProcessBlock(block);
      }
    }

    node_processor_.PostProcessGraph(graph);
  }

  NodeProcessor& node_processor() { return node_processor_; }
  const NodeProcessor& node_processor() const { return node_processor_; }

  void set_defer_deferred_blocks(bool defer) { defer_deferred_blocks_ = true; }

 private:
  ProcessingState GetCurrentState() {
    return ProcessingState(block_it_, &node_it_);
  }

  ProcessResult ProcessNodeBase(NodeBase* node, const ProcessingState& state) {
    switch (node->opcode()) {
#define CASE(OPCODE)                                        \
  case Opcode::k##OPCODE:                                   \
    if constexpr (!visit_identity_nodes &&                  \
                  Opcode::k##OPCODE == Opcode::kIdentity) { \
      return ProcessResult::kContinue;                      \
    }                                                       \
    PreProcess(node->Cast<OPCODE>(), state);                \
    return node_processor_.Process(node->Cast<OPCODE>(), state);

      NODE_BASE_LIST(CASE)
#undef CASE
    }
  }

  void PreProcess(NodeBase* node, const ProcessingState& state) {}

  NodeProcessor node_processor_;
  Graph* graph_;
  BlockConstIterator block_it_;
  NodeIterator node_it_;
  bool defer_deferred_blocks_ = false;
};

// A NodeProcessor that wraps multiple NodeProcessors, and forwards to each of
// them iteratively.
template <typename... Processors>
class NodeMultiProcessor;

template <>
class NodeMultiProcessor<> {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  void PreProcessBasicBlock(BasicBlock* block) {}
  ProcessResult Process(NodeBase* node, const ProcessingState& state) {
    return ProcessResult::kContinue;
  }
};

template <typename Processor, typename... Processors>
class NodeMultiProcessor<Processor, Processors...>
    : NodeMultiProcessor<Processors...> {
  using Base = NodeMultiProcessor<Processors...>;

 public:
  template <typename... Args>
  explicit NodeMultiProcessor(Processor&& processor, Args&&... processors)
      : Base(std::forward<Args>(processors)...),
        processor_(std::forward<Processor>(processor)) {}
  template <typename... Args>
  explicit NodeMultiProcessor(Args&&... processors)
      : Base(std::forward<Args>(processors)...) {}

  template <typename Node>
  ProcessResult Process(Node* node, const ProcessingState& state) {
    if (V8_UNLIKELY(processor_.Process(node, state) ==
                    ProcessResult::kRemove)) {
      return ProcessResult::kRemove;
    }
    return Base::Process(node, state);
  }
  void PreProcessGraph(Graph* graph) {
    processor_.PreProcessGraph(graph);
    Base::PreProcessGraph(graph);
  }
  void PostProcessGraph(Graph* graph) {
    // Post process in reverse order because that kind of makes sense.
    Base::PostProcessGraph(graph);
    processor_.PostProcessGraph(graph);
  }
  void PreProcessBasicBlock(BasicBlock* block) {
    processor_.PreProcessBasicBlock(block);
    Base::PreProcessBasicBlock(block);
  }

 private:
  Processor processor_;
};

template <typename... Processors>
using GraphMultiProcessor = GraphProcessor<NodeMultiProcessor<Processors...>>;

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_GRAPH_PROCESSOR_H_
