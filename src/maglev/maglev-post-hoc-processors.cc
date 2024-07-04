// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-post-hoc-processors.h"

#include "src/codegen/register-configuration.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-graph-printer.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph-verifier.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/maglev/maglev-ir.h"
#include "src/zone/zone-list-inl.h"

namespace v8::internal::maglev {

thread_local MaglevGraphLabeller* labeller_;

class ValueLocationConstraintProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  void PreProcessBasicBlock(BasicBlock* block) {}

#define DEF_PROCESS_NODE(NAME)                                      \
  ProcessResult Process(NAME* node, const ProcessingState& state) { \
    node->SetValueLocationConstraints();                            \
    return ProcessResult::kContinue;                                \
  }
  NODE_BASE_LIST(DEF_PROCESS_NODE)
#undef DEF_PROCESS_NODE
};

class DecompressedUseMarkingProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  void PreProcessBasicBlock(BasicBlock* block) {}

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
#ifdef V8_COMPRESS_POINTERS
    node->MarkTaggedInputsAsDecompressing();
#endif
    return ProcessResult::kContinue;
  }
};

class MaxCallDepthProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {
    graph->set_max_call_stack_args(max_call_stack_args_);
    graph->set_max_deopted_stack_size(max_deopted_stack_size_);
  }
  void PreProcessBasicBlock(BasicBlock* block) {}

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    if constexpr (NodeT::kProperties.is_call() ||
                  NodeT::kProperties.needs_register_snapshot()) {
      int node_stack_args = node->MaxCallStackArgs();
      if constexpr (NodeT::kProperties.needs_register_snapshot()) {
        // Pessimistically assume that we'll push all registers in deferred
        // calls.
        node_stack_args +=
            kAllocatableGeneralRegisterCount + kAllocatableDoubleRegisterCount;
      }
      max_call_stack_args_ = std::max(max_call_stack_args_, node_stack_args);
    }
    if constexpr (NodeT::kProperties.can_eager_deopt()) {
      UpdateMaxDeoptedStackSize(node->eager_deopt_info());
    }
    if constexpr (NodeT::kProperties.can_lazy_deopt()) {
      UpdateMaxDeoptedStackSize(node->lazy_deopt_info());
    }
    return ProcessResult::kContinue;
  }

 private:
  void UpdateMaxDeoptedStackSize(DeoptInfo* deopt_info) {
    const DeoptFrame* deopt_frame = &deopt_info->top_frame();
    int frame_size = 0;
    if (deopt_frame->type() == DeoptFrame::FrameType::kInterpretedFrame) {
      if (&deopt_frame->as_interpreted().unit() == last_seen_unit_) return;
      last_seen_unit_ = &deopt_frame->as_interpreted().unit();
      frame_size = deopt_frame->as_interpreted().unit().max_arguments() *
                   kSystemPointerSize;
    }

    do {
      frame_size += ConservativeFrameSize(deopt_frame);
      deopt_frame = deopt_frame->parent();
    } while (deopt_frame != nullptr);
    max_deopted_stack_size_ = std::max(frame_size, max_deopted_stack_size_);
  }
  int ConservativeFrameSize(const DeoptFrame* deopt_frame) {
    switch (deopt_frame->type()) {
      case DeoptFrame::FrameType::kInterpretedFrame: {
        auto info = UnoptimizedFrameInfo::Conservative(
            deopt_frame->as_interpreted().unit().parameter_count(),
            deopt_frame->as_interpreted().unit().register_count());
        return info.frame_size_in_bytes();
      }
      case DeoptFrame::FrameType::kConstructInvokeStubFrame: {
        return FastConstructStubFrameInfo::Conservative().frame_size_in_bytes();
      }
      case DeoptFrame::FrameType::kInlinedArgumentsFrame: {
        return std::max(
            0,
            static_cast<int>(
                deopt_frame->as_inlined_arguments().arguments().size() -
                deopt_frame->as_inlined_arguments().unit().parameter_count()) *
                kSystemPointerSize);
      }
      case DeoptFrame::FrameType::kBuiltinContinuationFrame: {
        // PC + FP + Closure + Params + Context
        const RegisterConfiguration* config = RegisterConfiguration::Default();
        auto info = BuiltinContinuationFrameInfo::Conservative(
            deopt_frame->as_builtin_continuation().parameters().length(),
            Builtins::CallInterfaceDescriptorFor(
                deopt_frame->as_builtin_continuation().builtin_id()),
            config);
        return info.frame_size_in_bytes();
      }
    }
  }

  int max_call_stack_args_ = 0;
  int max_deopted_stack_size_ = 0;
  // Optimize UpdateMaxDeoptedStackSize to not re-calculate if it sees the same
  // compilation unit multiple times in a row.
  const MaglevCompilationUnit* last_seen_unit_ = nullptr;
};

template <typename NodeT>
constexpr bool CanBeStoreToNonEscapedObject() {
  return std::is_same_v<NodeT, StoreMap> ||
         std::is_same_v<NodeT, StoreTaggedFieldWithWriteBarrier> ||
         std::is_same_v<NodeT, StoreTaggedFieldNoWriteBarrier> ||
         std::is_same_v<NodeT, StoreFloat64>;
}

class AnyUseMarkingProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PreProcessBasicBlock(BasicBlock* block) {}

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    if constexpr (IsValueNode(Node::opcode_of<NodeT>) &&
                  (!NodeT::kProperties.is_required_when_unused() ||
                   std::is_same_v<ArgumentsElements, NodeT>)) {
      if (!node->is_used()) {
        if (!node->unused_inputs_were_visited()) {
          DropInputUses(node);
        }
        return ProcessResult::kRemove;
      }
    }

    if constexpr (CanBeStoreToNonEscapedObject<NodeT>()) {
      if (node->input(0).node()->template Is<InlinedAllocation>()) {
        stores_to_allocations_.push_back(node);
      }
    }

    return ProcessResult::kContinue;
  }

  void PostProcessGraph(Graph* graph) {
    RunEscapeAnalysis(graph);
    DropUseOfValueInStoresToCapturedAllocations();
  }

 private:
  std::vector<Node*> stores_to_allocations_;

  void EscapeAllocation(Graph* graph, InlinedAllocation* alloc,
                        Graph::AllocationDependencies& deps) {
    if (alloc->HasBeenAnalysed() && alloc->HasEscaped()) return;
    alloc->SetEscaped();
    for (auto dep : deps) {
      EscapeAllocation(graph, dep, graph->allocations().find(dep)->second);
    }
  }

  void VerifyEscapeAnalysis(Graph* graph) {
#ifdef DEBUG
    for (auto it : graph->allocations()) {
      auto alloc = it.first;
      DCHECK(alloc->HasBeenAnalysed());
      if (alloc->HasEscaped()) {
        for (auto dep : it.second) {
          DCHECK(dep->HasEscaped());
        }
      }
    }
#endif  // DEBUG
  }

  void RunEscapeAnalysis(Graph* graph) {
    for (auto it : graph->allocations()) {
      auto alloc = it.first;
      if (alloc->HasBeenAnalysed()) continue;
      // Check if all its uses are non escaping.
      if (alloc->IsEscaping()) {
        // Escape this allocation and all its dependencies.
        EscapeAllocation(graph, alloc, it.second);
      } else {
        // Try to capture the allocation. This can still change if a escaped
        // allocation has this value as one of its dependencies.
        alloc->SetElided();
      }
    }
    // Check that we've reached a fixpoint.
    VerifyEscapeAnalysis(graph);
  }

  void DropUseOfValueInStoresToCapturedAllocations() {
    for (Node* node : stores_to_allocations_) {
      InlinedAllocation* alloc =
          node->input(0).node()->Cast<InlinedAllocation>();
      // Since we don't analyze if allocations will escape until a fixpoint,
      // this could drop an use of an allocation and turn it non-escaping.
      if (alloc->HasBeenElided()) {
        // Skip first input.
        for (int i = 1; i < node->input_count(); i++) {
          DropInputUses(node->input(i));
        }
      }
    }
  }

  void DropInputUses(Input& input) {
    ValueNode* input_node = input.node();
    if (input_node->properties().is_required_when_unused() &&
        !input_node->Is<ArgumentsElements>())
      return;
    input_node->remove_use();
    if (!input_node->is_used() && !input_node->unused_inputs_were_visited()) {
      DropInputUses(input_node);
    }
  }

  void DropInputUses(ValueNode* node) {
    for (Input& input : *node) {
      DropInputUses(input);
    }
    DCHECK(!node->properties().can_eager_deopt());
    DCHECK(!node->properties().can_lazy_deopt());
    node->mark_unused_inputs_visited();
  }
};
class DeadNodeSweepingProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  void PreProcessBasicBlock(BasicBlock* block) {}

  ProcessResult Process(AllocationBlock* node, const ProcessingState& state) {
    // Note: this need to be done before ValueLocationConstraintProcessor, since
    // it access the allocation offsets.
    int size = 0;
    for (auto alloc : node->allocation_list()) {
      if (alloc->HasEscaped()) {
        alloc->set_offset(size);
        size += alloc->size();
      }
    }
    // ... and update its size.
    node->set_size(size);
    // If size is zero, then none of the inlined allocations have escaped, we
    // can remove the allocation block.
    if (size == 0) return ProcessResult::kRemove;
    return ProcessResult::kContinue;
  }

  ProcessResult Process(InlinedAllocation* node, const ProcessingState& state) {
    // Remove inlined allocation that became non-escaping.
    if (!node->HasEscaped()) {
      if (v8_flags.trace_maglev_escape_analysis) {
        std::cout << "* Removing allocation node "
                  << PrintNodeLabel(labeller_, node) << std::endl;
      }
      return ProcessResult::kRemove;
    }
    return ProcessResult::kContinue;
  }

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    if constexpr (IsValueNode(Node::opcode_of<NodeT>) &&
                  (!NodeT::kProperties.is_required_when_unused() ||
                   std::is_same_v<ArgumentsElements, NodeT>)) {
      if (!node->is_used()) {
        if constexpr (std::is_same_v<NodeT, Phi>) {
          // The UseMarkingProcessor will clear dead forward jump Phis eagerly,
          // so the only dead phis that should remain are loop and exception
          // phis.
          DCHECK(node->is_loop_phi() || node->is_exception_phi());
        }
        return ProcessResult::kRemove;
      }
      return ProcessResult::kContinue;
    }

    if constexpr (CanBeStoreToNonEscapedObject<NodeT>()) {
      if (InlinedAllocation* object =
              node->input(0).node()->template TryCast<InlinedAllocation>()) {
        if (!object->HasEscaped()) {
          if (v8_flags.trace_maglev_escape_analysis) {
            std::cout << "* Removing store node "
                      << PrintNodeLabel(labeller_, node) << " to allocation "
                      << PrintNodeLabel(labeller_, object) << std::endl;
          }
          return ProcessResult::kRemove;
        }
      }
    }
    return ProcessResult::kContinue;
  }
};

class LiveRangeAndNextUseProcessor {
 public:
  explicit LiveRangeAndNextUseProcessor(MaglevCompilationInfo* compilation_info)
      : compilation_info_(compilation_info) {}

  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) { DCHECK(loop_used_nodes_.empty()); }
  void PreProcessBasicBlock(BasicBlock* block) {
    if (!block->has_state()) return;
    if (block->state()->is_loop()) {
      loop_used_nodes_.push_back(
          LoopUsedNodes{{}, kInvalidNodeId, kInvalidNodeId, block});
    }
  }

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    node->set_id(next_node_id_++);
    LoopUsedNodes* loop_used_nodes = GetCurrentLoopUsedNodes();
    if (loop_used_nodes && node->properties().is_call() &&
        loop_used_nodes->header->has_state()) {
      if (loop_used_nodes->first_call == kInvalidNodeId) {
        loop_used_nodes->first_call = node->id();
      }
      loop_used_nodes->last_call = node->id();
    }
    MarkInputUses(node, state);
    return ProcessResult::kContinue;
  }

  template <typename NodeT>
  void MarkInputUses(NodeT* node, const ProcessingState& state) {
    LoopUsedNodes* loop_used_nodes = GetCurrentLoopUsedNodes();
    // Mark input uses in the same order as inputs are assigned in the register
    // allocator (see StraightForwardRegisterAllocator::AssignInputs).
    node->ForAllInputsInRegallocAssignmentOrder(
        [&](NodeBase::InputAllocationPolicy, Input* input) {
          MarkUse(input->node(), node->id(), input, loop_used_nodes);
        });
    if constexpr (NodeT::kProperties.can_eager_deopt()) {
      MarkCheckpointNodes(node, node->eager_deopt_info(), loop_used_nodes,
                          state);
    }
    if constexpr (NodeT::kProperties.can_lazy_deopt()) {
      MarkCheckpointNodes(node, node->lazy_deopt_info(), loop_used_nodes,
                          state);
    }
  }

  void MarkInputUses(Phi* node, const ProcessingState& state) {
    // Don't mark Phi uses when visiting the node, because of loop phis.
    // Instead, they'll be visited while processing Jump/JumpLoop.
  }

  // Specialize the two unconditional jumps to extend their Phis' inputs' live
  // ranges.

  void MarkInputUses(JumpLoop* node, const ProcessingState& state) {
    int i = state.block()->predecessor_id();
    BasicBlock* target = node->target();
    uint32_t use = node->id();

    DCHECK(!loop_used_nodes_.empty());
    LoopUsedNodes loop_used_nodes = std::move(loop_used_nodes_.back());
    loop_used_nodes_.pop_back();

    LoopUsedNodes* outer_loop_used_nodes = GetCurrentLoopUsedNodes();

    if (target->has_phi()) {
      for (Phi* phi : *target->phis()) {
        DCHECK(phi->is_used());
        ValueNode* input = phi->input(i).node();
        MarkUse(input, use, &phi->input(i), outer_loop_used_nodes);
      }
    }

    DCHECK_EQ(loop_used_nodes.header, target);
    if (!loop_used_nodes.used_nodes.empty()) {
      // Try to avoid unnecessary reloads or spills across the back-edge based
      // on use positions and calls inside the loop.
      ZonePtrList<ValueNode>& reload_hints =
          loop_used_nodes.header->reload_hints();
      ZonePtrList<ValueNode>& spill_hints =
          loop_used_nodes.header->spill_hints();
      for (auto p : loop_used_nodes.used_nodes) {
        // If the node is used before the first call and after the last call,
        // keep it in a register across the back-edge.
        if (p.second.first_register_use != kInvalidNodeId &&
            (loop_used_nodes.first_call == kInvalidNodeId ||
             (p.second.first_register_use <= loop_used_nodes.first_call &&
              p.second.last_register_use > loop_used_nodes.last_call))) {
          reload_hints.Add(p.first, compilation_info_->zone());
        }
        // If the node is not used, or used after the first call and before the
        // last call, keep it spilled across the back-edge.
        if (p.second.first_register_use == kInvalidNodeId ||
            (loop_used_nodes.first_call != kInvalidNodeId &&
             p.second.first_register_use > loop_used_nodes.first_call &&
             p.second.last_register_use <= loop_used_nodes.last_call)) {
          spill_hints.Add(p.first, compilation_info_->zone());
        }
      }

      // Uses of nodes in this loop may need to propagate to an outer loop, so
      // that they're lifetime is extended there too.
      // TODO(leszeks): We only need to extend the lifetime in one outermost
      // loop, allow nodes to be "moved" between lifetime extensions.
      base::Vector<Input> used_node_inputs =
          compilation_info_->zone()->AllocateVector<Input>(
              loop_used_nodes.used_nodes.size());
      int i = 0;
      for (auto& [used_node, info] : loop_used_nodes.used_nodes) {
        Input* input = new (&used_node_inputs[i++]) Input(used_node);
        MarkUse(used_node, use, input, outer_loop_used_nodes);
      }
      node->set_used_nodes(used_node_inputs);
    }
  }
  void MarkInputUses(Jump* node, const ProcessingState& state) {
    MarkJumpInputUses(node->id(), node->target(), state);
  }
  void MarkInputUses(CheckpointedJump* node, const ProcessingState& state) {
    MarkJumpInputUses(node->id(), node->target(), state);
  }
  void MarkJumpInputUses(uint32_t use, BasicBlock* target,
                         const ProcessingState& state) {
    int i = state.block()->predecessor_id();
    if (!target->has_phi()) return;
    LoopUsedNodes* loop_used_nodes = GetCurrentLoopUsedNodes();
    Phi::List& phis = *target->phis();
    for (auto it = phis.begin(); it != phis.end();) {
      Phi* phi = *it;
      if (!phi->is_used()) {
        // Skip unused phis -- we're processing phis out of order with the dead
        // node sweeping processor, so we will still observe unused phis here.
        // We can eagerly remove them while we're at it so that the dead node
        // sweeping processor doesn't have to revisit them.
        it = phis.RemoveAt(it);
      } else {
        ValueNode* input = phi->input(i).node();
        MarkUse(input, use, &phi->input(i), loop_used_nodes);
        ++it;
      }
    }
  }

 private:
  struct NodeUse {
    // First and last register use inside a loop.
    NodeIdT first_register_use;
    NodeIdT last_register_use;
  };

  struct LoopUsedNodes {
    std::map<ValueNode*, NodeUse> used_nodes;
    NodeIdT first_call;
    NodeIdT last_call;
    BasicBlock* header;
  };

  LoopUsedNodes* GetCurrentLoopUsedNodes() {
    if (loop_used_nodes_.empty()) return nullptr;
    return &loop_used_nodes_.back();
  }

  void MarkUse(ValueNode* node, uint32_t use_id, InputLocation* input,
               LoopUsedNodes* loop_used_nodes) {
    DCHECK(!node->Is<Identity>());

    node->record_next_use(use_id, input);

    // If we are in a loop, loop_used_nodes is non-null. In this case, check if
    // the incoming node is from outside the loop, and make sure to extend its
    // lifetime to the loop end if yes.
    if (loop_used_nodes) {
      // If the node's id is smaller than the smallest id inside the loop, then
      // it must have been created before the loop. This means that it's alive
      // on loop entry, and therefore has to be alive across the loop back edge
      // too.
      if (node->id() < loop_used_nodes->header->first_id()) {
        auto [it, info] = loop_used_nodes->used_nodes.emplace(
            node, NodeUse{kInvalidNodeId, kInvalidNodeId});
        if (input->operand().IsUnallocated()) {
          const auto& operand =
              compiler::UnallocatedOperand::cast(input->operand());
          if (operand.HasRegisterPolicy() || operand.HasFixedRegisterPolicy() ||
              operand.HasFixedFPRegisterPolicy()) {
            if (it->second.first_register_use == kInvalidNodeId) {
              it->second.first_register_use = use_id;
            }
            it->second.last_register_use = use_id;
          }
        }
      }
    }
  }

  void MarkCheckpointNodes(NodeBase* node, EagerDeoptInfo* deopt_info,
                           LoopUsedNodes* loop_used_nodes,
                           const ProcessingState& state) {
    int use_id = node->id();
    detail::DeepForEachInputRemovingIdentities(
        deopt_info, [&](ValueNode* node, InputLocation* input) {
          MarkUse(node, use_id, input, loop_used_nodes);
        });
  }
  void MarkCheckpointNodes(NodeBase* node, LazyDeoptInfo* deopt_info,
                           LoopUsedNodes* loop_used_nodes,
                           const ProcessingState& state) {
    int use_id = node->id();
    detail::DeepForEachInputRemovingIdentities(
        deopt_info, [&](ValueNode* node, InputLocation* input) {
          MarkUse(node, use_id, input, loop_used_nodes);
        });
  }

  MaglevCompilationInfo* compilation_info_;
  uint32_t next_node_id_ = kFirstValidNodeId;
  std::vector<LoopUsedNodes> loop_used_nodes_;
};

void RunPostHocProcessors(LocalIsolate* local_isolate,
                          MaglevCompilationInfo* compilation_info,
                          Graph* graph) {
  if (compilation_info->has_graph_labeller()) {
    labeller_ = compilation_info->graph_labeller();
  }

  {
    // Post-hoc optimisation:
    //   - Dead node marking
    //   - Cleaning up identity nodes
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.DeadCodeMarking");
    GraphMultiProcessor<AnyUseMarkingProcessor> processor;
    processor.ProcessGraph(graph);
  }

  if (v8_flags.print_maglev_graphs) {
    UnparkedScopeIfOnBackground unparked_scope(local_isolate->heap());
    std::cout << "After use marking" << std::endl;
    PrintGraph(std::cout, compilation_info, graph);
  }

#ifdef DEBUG
  {
    GraphProcessor<MaglevGraphVerifier> verifier(compilation_info);
    verifier.ProcessGraph(graph);
  }
#endif

  {
    // Preprocessing for register allocation and code gen:
    //   - Remove dead nodes
    //   - Collect input/output location constraints
    //   - Find the maximum number of stack arguments passed to calls
    //   - Collect use information, for SSA liveness and next-use distance.
    //   - Mark
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.NodeProcessing");
    GraphMultiProcessor<DeadNodeSweepingProcessor,
                        ValueLocationConstraintProcessor, MaxCallDepthProcessor,
                        LiveRangeAndNextUseProcessor,
                        DecompressedUseMarkingProcessor>
        processor(LiveRangeAndNextUseProcessor{compilation_info});
    processor.ProcessGraph(graph);
  }

  if (v8_flags.print_maglev_graphs) {
    UnparkedScopeIfOnBackground unparked_scope(local_isolate->heap());
    std::cout << "After register allocation pre-processing" << std::endl;
    PrintGraph(std::cout, compilation_info, graph);
  }
}

}  // namespace v8::internal::maglev
