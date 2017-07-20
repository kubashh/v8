// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NEW_ESCAPE_ANALYSIS_REDUCER_H_
#define V8_COMPILER_NEW_ESCAPE_ANALYSIS_REDUCER_H_

#include "src/base/compiler-specific.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/new-escape-analysis.h"
#include "src/globals.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class JSGraph;

class NodePtr {
 public:
  explicit NodePtr(Node* node) : node_(node) {}
  bool operator==(NodePtr other) const {
    Node* a = node_;
    Node* b = other.node_;
    DCHECK_NOT_NULL(a);
    DCHECK_NOT_NULL(b);
    DCHECK_NOT_NULL(a->op());
    DCHECK_NOT_NULL(b->op());
    if (!a->op()->Equals(b->op())) return false;
    if (a->InputCount() != b->InputCount()) return false;
    Node::Inputs aInputs = a->inputs();
    Node::Inputs bInputs = b->inputs();

    auto aIt = aInputs.begin();
    auto bIt = bInputs.begin();
    auto aEnd = aInputs.end();

    for (; aIt != aEnd; ++aIt, ++bIt) {
      DCHECK_NOT_NULL(*aIt);
      DCHECK_NOT_NULL(*bIt);
      if ((*aIt)->id() != (*bIt)->id()) return false;
    }
    return true;
  }
  bool operator!=(NodePtr other) const { return !(*this == other); }
  Node* ptr() const { return node_; }
  friend V8_INLINE size_t hash_value(NodePtr node) {
    size_t h = base::hash_combine(node.ptr()->op()->HashCode(),
                                  node.ptr()->InputCount());
    for (Node* input : node.ptr()->inputs()) {
      h = base::hash_combine(h, input->id());
    }
    return h;
  }

 private:
  Node* node_;
};

class NodeHashCache {
 public:
  NodeHashCache(Graph* graph, Zone* zone)
      : graph_(graph), cache_(zone), temp_nodes_(zone) {}

  class Constructor {
   public:
    Constructor(NodeHashCache* cache, Node* from)
        : node_cache_(cache), from_(from), tmp_(nullptr) {}
    Constructor(NodeHashCache* cache, const Operator* op, int input_count,
                Node** inputs, Type* type)
        : node_cache_(cache), from_(nullptr) {
      if (node_cache_->temp_nodes_.size() > 0) {
        tmp_ = node_cache_->temp_nodes_.back();
        node_cache_->temp_nodes_.pop_back();
        int tmp_input_count = tmp_->InputCount();
        if (input_count <= tmp_input_count) {
          tmp_->TrimInputCount(input_count);
        }
        for (int i = 0; i < input_count; ++i) {
          if (i < tmp_input_count) {
            tmp_->ReplaceInput(i, inputs[i]);
          } else {
            tmp_->AppendInput(node_cache_->graph_->zone(), inputs[i]);
          }
        }
        NodeProperties::ChangeOp(tmp_, op);
      } else {
        tmp_ = node_cache_->graph_->NewNode(op, input_count, inputs);
      }
      NodeProperties::SetType(tmp_, type);
    }

    void ReplaceValueInput(Node* input, int i) {
      if (!tmp_ && input == NodeProperties::GetValueInput(from_, i)) return;
      Node* node = MutableNode();
      NodeProperties::ReplaceValueInput(node, input, i);
    }
    void ReplaceInput(Node* input, int i) {
      if (!tmp_ && input == from_->InputAt(i)) return;
      Node* node = MutableNode();
      node->ReplaceInput(i, input);
    }

    // Obtain the mutated node or a cached copy.
    Node* Get() {
      if (tmp_ == nullptr) {
        if (Node* node = node_cache_->Query(from_)) {
          return node;
        } else {
          return from_;
        }
      }
      if (Node* node = node_cache_->Query(tmp_)) {
        node_cache_->temp_nodes_.push_back(tmp_);
        tmp_ = nullptr;
        return node;
      }
      Node* node = tmp_;
      tmp_ = nullptr;
      node_cache_->Insert(node);
      return node;
    }

   private:
    Node* MutableNode() {
      if (!tmp_) {
        DCHECK(from_ != nullptr);
        if (node_cache_->temp_nodes_.size() > 0) {
          tmp_ = node_cache_->temp_nodes_.back();
          node_cache_->temp_nodes_.pop_back();
          int from_input_count = from_->InputCount();
          int tmp_input_count = tmp_->InputCount();
          if (from_input_count <= tmp_input_count) {
            tmp_->TrimInputCount(from_input_count);
          }
          for (int i = 0; i < from_input_count; ++i) {
            if (i < tmp_input_count) {
              tmp_->ReplaceInput(i, from_->InputAt(i));
            } else {
              tmp_->AppendInput(node_cache_->graph_->zone(), from_->InputAt(i));
            }
          }
          NodeProperties::SetType(tmp_, NodeProperties::GetType(from_));
          NodeProperties::ChangeOp(tmp_, from_->op());
        } else {
          tmp_ = node_cache_->graph_->CloneNode(from_);
        }
      }
      return tmp_;
    }

    NodeHashCache* node_cache_;
    // Original node, copied on write.
    Node* from_;
    // Temporary node used for mutations, can be recycled if cache is hit.
    Node* tmp_;
  };

  Node* Query(Node* node) {
    auto it = cache_.find(NodePtr(node));
    if (it != cache_.end()) {
      return it->ptr();
    } else {
      return nullptr;
    }
  }

  void Insert(Node* node) { cache_.insert(NodePtr(node)); }

 private:
  Graph* graph_;
  ZoneUnorderedSet<NodePtr> cache_;
  ZoneVector<Node*> temp_nodes_;
};

class Deduplicator;

// Apply
class V8_EXPORT_PRIVATE NewEscapeAnalysisReducer final
    : public NON_EXPORTED_BASE(AdvancedReducer) {
 public:
  NewEscapeAnalysisReducer(Editor* editor, JSGraph* jsgraph,
                           EscapeAnalysisResult analysis_result, Zone* zone);

  Reduction Reduce(Node* node) override;
  const char* reducer_name() const override {
    return "NewEscapeAnalysisReducer";
  }
  void Finalize() override;

  // Verifies that all virtual allocation nodes have been dealt with. Run it
  // after this reducer has been applied. Has no effect in release mode.
  void VerifyReplacement() const;

 private:
  void ReduceFrameStateInputs(Node* node);
  Node* ReduceDeoptState(Node* node, Node* effect, Deduplicator* deduplicator);
  Node* ObjectIdNode(const VirtualObject* vobject);
  Node* MaybeGuard(Node* original, Node* replacement);

  JSGraph* jsgraph() const { return jsgraph_; }
  EscapeAnalysisResult analysis_result() const { return analysis_result_; }
  Zone* zone() const { return zone_; }

  JSGraph* const jsgraph_;
  EscapeAnalysisResult analysis_result_;
  ZoneVector<Node*> object_id_cache_;
  NodeHashCache node_cache_;
  ZoneSet<Node*> arguments_elements_;
  Zone* const zone_;

  DISALLOW_COPY_AND_ASSIGN(NewEscapeAnalysisReducer);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NEW_ESCAPE_ANALYSIS_REDUCER_H_
