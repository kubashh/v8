// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NEW_ESCAPE_ANALYSIS_H_
#define V8_COMPILER_NEW_ESCAPE_ANALYSIS_H_

#include "src/base/functional.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/persistent-map.h"
#include "src/globals.h"

#ifdef DEBUG
#define TRACE(...)                                    \
  do {                                                \
    if (FLAG_trace_turbo_escape) PrintF(__VA_ARGS__); \
  } while (false)
#else
#define TRACE(...)
#endif

namespace v8 {
namespace internal {
namespace compiler {

class CommonOperatorBuilder;

#ifdef DEBUG
class TraceScope {
 public:
  TraceScope(const char* name, Node* node) : name_(name), node_(node) {
    for (int i = 0; i < depth; ++i) TRACE("  ");
    TRACE("[ %s %s#%d\n", name, node->op()->mnemonic(), node->id());
    ++depth;
  }
  ~TraceScope() {
    --depth;
    for (int i = 0; i < depth; ++i) TRACE("  ");
    TRACE("] %s %s#%d\n", name_, node_->op()->mnemonic(), node_->id());
  }

 private:
  const char* name_;
  Node* node_;
  static thread_local int depth;
};
#define TRACE_FN(name, node) TraceScope __trace_scope_(name, node)
#else
#define TRACE_FN(name, node)
#endif

template <class T>
class Sidetable {
 public:
  explicit Sidetable(Zone* zone) : map_(zone) {}
  T& operator[](const Node* node) {
    NodeId id = node->id();
    if (id >= map_.size()) {
      map_.resize(id + 1);
    }
    return map_[id];
  }

 private:
  ZoneVector<T> map_;
};

template <class T>
class SparseSidetable {
 public:
  explicit SparseSidetable(Zone* zone, T def_value = T())
      : def_value_(std::move(def_value)), map_(zone) {}
  T& operator[](const Node* node) {
    return map_.insert(std::make_pair(node->id(), def_value_)).first->second;
  }

 private:
  T def_value_;
  ZoneUnorderedMap<NodeId, T> map_;
};

// {EffectGraphReducer} reduces up to a fixed point. It distinguishes changes to
// the effect output of a node from changes to the value output to reduce the
// number of revisitations.
class EffectGraphReducer {
 public:
  class Reduction {
   public:
    bool value_changed() const { return value_changed_; }
    void set_value_changed() { value_changed_ = true; }
    bool effect_changed() const { return effect_changed_; }
    void set_effect_changed() { effect_changed_ = true; }

   private:
    bool value_changed_ = false;
    bool effect_changed_ = false;
  };

  EffectGraphReducer(Graph* graph,
                     std::function<void(Node*, Reduction*)> reduce, Zone* zone);

  void ReduceGraph() { ReduceFrom(graph_->end()); }
  // Mark node for revisitation.
  void Revisit(Node* node);
  // Add a new root node to start reduction from. This is useful if the reducer
  // adds nodes that are not yet reachable, but should already be considered
  // part of the graph.
  void AddRoot(Node* node) {
    DCHECK_EQ(State::kUnvisited, state_.Get(node));
    state_.Set(node, State::kRevisit);
    revisit_.push(node);
  }

  bool Complete() { return stack_.empty() && revisit_.empty(); }

 private:
  struct NodeState {
    Node* node;
    int input_index;
  };
  void ReduceFrom(Node* node);
  enum class State : uint8_t { kUnvisited = 0, kRevisit, kOnStack, kVisited };
  const uint8_t kNumStates = static_cast<uint8_t>(State::kVisited) + 1;
  Graph* graph_;
  NodeMarker<State> state_;
  ZoneStack<Node*> revisit_;
  ZoneStack<NodeState> stack_;
  std::function<void(Node*, Reduction*)> reduce_;
};

// Keeps track of the changes to the current node during reduction.
class ReduceScope {
 public:
  typedef EffectGraphReducer::Reduction Reduction;
  explicit ReduceScope(Node* node, Reduction* reduction)
      : current_node_(node), reduction_(reduction) {}

 protected:
  Node* current_node() const { return current_node_; }
  Reduction* reduction() { return reduction_; }

 private:
  Node* current_node_;
  Reduction* reduction_;
};

// A variable is an abstract storage location, which is lowered to SSA values
// and phi nodes by {VariableTracker}.
class Variable {
 public:
  Variable() : id_(kInvalid) {}
  bool operator==(Variable other) const { return id_ == other.id_; }
  bool operator!=(Variable other) const { return id_ != other.id_; }
  bool operator<(Variable other) const { return id_ < other.id_; }
  static Variable Invalid() { return Variable(kInvalid); }
  friend V8_INLINE size_t hash_value(Variable v) {
    return base::hash_value(v.id_);
  }
  friend std::ostream& operator<<(std::ostream& os, Variable var) {
    return os << var.id_;
  }

 private:
  typedef int Id;
  explicit Variable(Id id) : id_(id) {}
  Id id_;
  static const Id kInvalid = -1;

  friend class VariableTracker;
};

// A VariableTracker object keeps track of the values of variables at all points
// of the effect chain and introduces new phi nodes when necessary.
class VariableTracker {
 private:
  // The state of all variables at one point in the effect chain.
  class State {
    typedef PersistentMap<Variable, Node*> Map;

   public:
    explicit State(Zone* zone) : map_(zone) {}
    Node* Get(Variable var) const {
      CHECK(var != Variable::Invalid());
      return map_.Get(var);
    }
    void Set(Variable var, Node* node) {
      CHECK(var != Variable::Invalid());
      return map_.Set(var, node);
    }
    Map::iterator begin() const { return map_.begin(); }
    Map::iterator end() const { return map_.end(); }
    bool operator!=(const State& other) const { return map_ != other.map_; }

   private:
    Map map_;
  };

 public:
  VariableTracker(JSGraph* graph, EffectGraphReducer* reducer, Zone* zone);
  Variable NewVariable() { return Variable(next_variable_++); }
  Node* Get(Variable var, Node* effect) { return table_[effect].Get(var); }
  Zone* zone() { return zone_; }

  class Scope : public ReduceScope {
   public:
    Scope(VariableTracker* tracker, Node* node, Reduction* reduction);
    ~Scope();
    Node* Get(Variable var) { return current_state_.Get(var); }
    void Set(Variable var, Node* node) { current_state_.Set(var, node); }

   private:
    VariableTracker* states_;
    State current_state_;
  };

 private:
  State MergeInputs(Node* effect_phi);
  Zone* zone_;
  JSGraph* graph_;
  SparseSidetable<State> table_;
  ZoneVector<Node*> buffer_;
  EffectGraphReducer* reducer_;
  int next_variable_ = 0;

  DISALLOW_COPY_AND_ASSIGN(VariableTracker);
};

// An object that can track the nodes in the graph whose current reduction
// depends on the value of the object.
class Dependable : public ZoneObject {
 public:
  explicit Dependable(Zone* zone) : dependants_(zone) {}
  void AddDependency(Node* node) { dependants_.push_back(node); }
  void RevisitDependants(EffectGraphReducer* reducer) {
    for (Node* node : dependants_) {
      reducer->Revisit(node);
    }
    dependants_.clear();
  }

 private:
  ZoneVector<Node*> dependants_;
};

// A virtual object represents an allocation site and tracks the Variables
// associated with its fields as well as its global escape status.
class VirtualObject : public Dependable {
 public:
  typedef uint32_t Id;
  typedef ZoneVector<Variable>::const_iterator const_iterator;
  VirtualObject(VariableTracker* var_states, Id id, int size)
      : Dependable(var_states->zone()), id_(id), fields_(var_states->zone()) {
    DCHECK(size % kPointerSize == 0);
    TRACE("Creating VirtualObject id:%d size:%d\n", id, size);
    int num_fields = size / kPointerSize;
    fields_.reserve(num_fields);
    for (int i = 0; i < num_fields; ++i) {
      fields_.push_back(var_states->NewVariable());
    }
  }
  Maybe<Variable> FieldAt(int offset) const {
    DCHECK(offset % kPointerSize == 0);
    CHECK(!HasEscaped());
    if (offset >= size()) {
      // This can only happen in unreachable code.
      return Nothing<Variable>();
    }
    return Just(fields_.at(offset / kPointerSize));
  }
  Id id() const { return id_; }
  int size() const { return static_cast<int>(kPointerSize * fields_.size()); }
  // Escaped might mean that the object escaped to untracked memory or that it
  // is used in an operation that requires materialization.
  void SetEscaped() { escaped_ = true; }
  bool HasEscaped() const { return escaped_; }
  const_iterator begin() const { return fields_.begin(); }
  const_iterator end() const { return fields_.end(); }

 private:
  bool escaped_ = false;
  Id id_;
  ZoneVector<Variable> fields_;
};

// Encapsulates the current state of the escape analysis reducer to preserve
// invariants regarding changes and re-visitation.
class EscapeAnalysisTracker {
 public:
  EscapeAnalysisTracker(JSGraph* jsgraph, EffectGraphReducer* reducer,
                        Zone* zone)
      : virtual_objects_(zone),
        replacements_(zone),
        variable_states_(jsgraph, reducer, zone),
        jsgraph_(jsgraph),
        zone_(zone) {}

  class Scope : public VariableTracker::Scope {
   public:
    Scope(EffectGraphReducer* reducer, EscapeAnalysisTracker* tracker,
          Node* node, Reduction* reduction)
        : VariableTracker::Scope(&tracker->variable_states_, node, reduction),
          tracker_(tracker),
          reducer_(reducer) {}
    const VirtualObject* GetVirtualObject(Node* node) {
      VirtualObject* vobject = tracker_->virtual_objects_[node];
      if (vobject) vobject->AddDependency(current_node());
      return vobject;
    }
    // Create or retrieve a virtual object for the current node.
    const VirtualObject* InitVirtualObject(int size) {
      DCHECK(current_node()->opcode() == IrOpcode::kAllocate);
      VirtualObject* vobject = tracker_->virtual_objects_[current_node()];
      if (vobject) {
        CHECK(vobject->size() == size);
      } else {
        vobject = tracker_->NewVirtualObject(size);
      }
      vobject_ = vobject;
      return vobject;
    }

    void SetVirtualObject(Node* object) {
      vobject_ = tracker_->virtual_objects_[object];
    }

    void SetEscaped(Node* node) {
      if (VirtualObject* object = tracker_->virtual_objects_[node]) {
        if (object->HasEscaped()) return;
        TRACE("Setting %s#%d to escaped because of use by %s#%d\n",
              node->op()->mnemonic(), node->id(),
              current_node()->op()->mnemonic(), current_node()->id());
        object->SetEscaped();
        object->RevisitDependants(reducer_);
      }
    }
    // The inputs of the current node have to be accessed through the scope to
    // ensure that they respect the node replacements.
    Node* ValueInput(int i) {
      return tracker_->ResolveReplacement(
          NodeProperties::GetValueInput(current_node(), i));
    }
    Node* ContextInput() {
      return tracker_->ResolveReplacement(
          NodeProperties::GetContextInput(current_node()));
    }

    void SetReplacement(Node* replacement) {
      replacement_ = replacement;
      vobject_ =
          replacement ? tracker_->virtual_objects_[replacement] : nullptr;
      TRACE("Set %s#%d as replacement.\n", replacement->op()->mnemonic(),
            replacement->id());
    }

    void MarkForDeletion() { SetReplacement(tracker_->jsgraph()->Dead()); }

    ~Scope() {
      if (replacement_ != tracker_->replacements_[current_node()] ||
          vobject_ != tracker_->virtual_objects_[current_node()]) {
        reduction()->set_value_changed();
      }
      tracker_->replacements_[current_node()] = replacement_;
      tracker_->virtual_objects_[current_node()] = vobject_;
    }

   private:
    EscapeAnalysisTracker* tracker_;
    EffectGraphReducer* reducer_;
    VirtualObject* vobject_ = nullptr;
    Node* replacement_ = nullptr;
  };

  Node* GetReplacementOf(Node* node) { return replacements_[node]; }
  Node* ResolveReplacement(Node* node) {
    if (Node* repl = GetReplacementOf(node)) {
      // Replacements cannot have replacements. This is important to ensure
      // re-visitation: If a replacement is replaced, then all nodes accessing
      // the replacement have to be updated.
      DCHECK_NULL(GetReplacementOf(repl));
      return repl;
    }
    return node;
  }

 protected:
  const size_t kMaxTrackedObjects = 100;
  JSGraph* jsgraph() { return jsgraph_; }

 private:
  friend class EscapeAnalysisResult;

  VirtualObject* NewVirtualObject(int size) {
    if (next_object_id_ >= kMaxTrackedObjects) return nullptr;
    return new (zone_)
        VirtualObject(&variable_states_, next_object_id_++, size);
  }

  SparseSidetable<VirtualObject*> virtual_objects_;
  Sidetable<Node*> replacements_;
  VariableTracker variable_states_;
  VirtualObject::Id next_object_id_ = 0;
  JSGraph* jsgraph_;
  Zone* zone_;

  DISALLOW_COPY_AND_ASSIGN(EscapeAnalysisTracker);
};

class EscapeAnalysisResult {
 public:
  explicit EscapeAnalysisResult(EscapeAnalysisTracker* tracker)
      : tracker_(tracker) {}

  const VirtualObject* GetVirtualObject(Node* node) {
    return tracker_->virtual_objects_[node];
  }

  Node* GetVirtualObjectField(const VirtualObject* vobject, int field,
                              Node* effect) {
    return tracker_->variable_states_.Get(vobject->FieldAt(field).FromJust(),
                                          effect);
  }

  Node* GetReplacementOf(Node* node) {
    return tracker_->GetReplacementOf(node);
  }

 private:
  EscapeAnalysisTracker* tracker_;
};

class V8_EXPORT_PRIVATE NewEscapeAnalysis final
    : public NON_EXPORTED_BASE(EffectGraphReducer) {
 public:
  NewEscapeAnalysis(JSGraph* jsgraph, Zone* zone)
      : EffectGraphReducer(jsgraph->graph(),
                           [this](Node* node, Reduction* reduction) {
                             Reduce(node, reduction);
                           },
                           zone),
        tracker_(jsgraph, this, zone),
        jsgraph_(jsgraph) {}

  EscapeAnalysisResult analysis_result() {
    DCHECK(Complete());
    return EscapeAnalysisResult(&tracker_);
  }

 private:
  void Reduce(Node* node, Reduction* reduction);
  void ReduceNode(const Operator* op, EscapeAnalysisTracker::Scope* current);
  JSGraph* jsgraph() { return jsgraph_; }
  EscapeAnalysisTracker tracker_;
  JSGraph* jsgraph_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NEW_ESCAPE_ANALYSIS_H_
