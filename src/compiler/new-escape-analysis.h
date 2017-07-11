// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NEW_ESCAPE_ANALYSIS_H_
#define V8_COMPILER_NEW_ESCAPE_ANALYSIS_H_

#include "src/base/functional.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/persistent.h"
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
  explicit Sidetable(Zone* zone, T def_value = T())
      : def_value_(std::move(def_value)), map_(zone) {}

  T& operator[](const Node* node) {
    NodeId id = node->id();
    while (map_.size() <= id) map_.push_back(def_value_);
    return map_[id];
  }

 private:
  T def_value_;
  ZoneVector<T> map_;
};

class EffectGraphReducer {
 public:
  EffectGraphReducer(Graph* graph, Zone* zone)
      : graph_(graph),
        state_(graph, kNumStates),
        revisit_(zone),
        roots_(zone) {}

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

  void ReduceGraph() {
    roots_.push(graph_->end());
    while (!roots_.empty()) {
      Node* root = roots_.top();
      roots_.pop();
      ReduceFrom(root);
    }
  }

  void Revisit(Node* node);
  void AddRoot(Node* node) { roots_.push(node); }

 protected:
  virtual Reduction Reduce(Node* node) = 0;

 private:
  void ReduceFrom(Node* node);
  enum class State : uint8_t { kUnvisited = 0, kRevisit, kOnStack, kVisited };
  const uint8_t kNumStates = static_cast<uint8_t>(State::kVisited) + 1;
  Graph* graph_;
  NodeMarker<State> state_;
  ZoneStack<Node*> revisit_;
  ZoneStack<Node*> roots_;
};

class ReduceScope {
 public:
  using Reduction = EffectGraphReducer::Reduction;
  explicit ReduceScope(Node* node, Reduction* reduction)
      : current_node_(node), reduction_(reduction) {}

 protected:
  Node* current_node() const { return current_node_; }
  Reduction* reduction() { return reduction_; }

 private:
  Node* current_node_;
  Reduction* reduction_;
};

class Variable {
 public:
  Variable() : id_(kInvalid) {}
  bool operator==(Variable other) const { return id_ == other.id_; }
  bool operator!=(Variable other) const { return id_ != other.id_; }
  bool operator<(Variable other) const { return id_ < other.id_; }
  friend V8_INLINE size_t hash_value(Variable v) {
    return base::hash_value(v.id_);
  }
  static Variable Invalid() { return Variable(kInvalid); }
  friend std::ostream& operator<<(std::ostream& os, Variable var) {
    return os << var.id_;
  }

 private:
  using Id = int;
  explicit Variable(Id id) : id_(id) {}
  Id id_;
  static const Id kInvalid = -1;

  friend class VariableStates;
};

class VariableStates {
 private:
  using State = PersistentMap<Variable, Node*>;

 public:
  VariableStates(JSGraph* graph, EffectGraphReducer* reducer, Zone* zone)
      : zone_(zone),
        graph_(graph),
        table_(zone, State(zone)),
        reducer_(reducer) {}

  Variable NewVariable() { return Variable(next_variable_++); }
  Node* Get(Variable var, Node* effect) { return Get(var, table_[effect]); }
  Zone* zone() { return zone_; }

  class Scope : public ReduceScope {
   public:
    Scope(VariableStates* states, Node* node, Reduction* reduction)
        : ReduceScope(node, reduction),
          states_(states),
          current_state_(states->zone_) {
      switch (node->opcode()) {
        case IrOpcode::kEffectPhi:
          current_state_ = states_->MergeInputs(node);
          break;
        default:
          int effect_inputs = node->op()->EffectInputCount();
          if (effect_inputs == 1) {
            current_state_ =
                states_->table_[NodeProperties::GetEffectInput(node, 0)];
          } else {
            DCHECK_EQ(0, effect_inputs);
          }
      }
    }
    ~Scope() {
      if (!reduction()->effect_changed() &&
          states_->table_[current_node()] != current_state_) {
        reduction()->set_effect_changed();
      }
      states_->table_[current_node()] = current_state_;
    }
    Node* Get(Variable var) { return states_->Get(var, current_state_); }
    void Set(Variable var, Node* node) {
      states_->Set(var, node, &current_state_);
    }

   private:
    VariableStates* states_;
    State current_state_;
  };

 private:
  Node* Get(Variable var, const State& state) {
    if (var == Variable::Invalid()) {
      return graph_->Dead();
    }
    return state[var];
  }
  void Set(Variable var, Node* node, State* state) {
    if (var == Variable::Invalid()) return;
    (*state)[var] = node;
  }
  State MergeInputs(Node* effect_phi);
  Zone* zone_;
  JSGraph* graph_;
  Sidetable<State> table_;
  EffectGraphReducer* reducer_;
  int next_variable_ = 0;
};

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

class VirtualObject : public Dependable {
 public:
  using Id = uint32_t;
  using const_iterator = ZoneVector<Variable>::const_iterator;
  VirtualObject(VariableStates* var_states, Id id, int size)
      : Dependable(var_states->zone()), id_(id), fields_(var_states->zone()) {
    DCHECK(size % kPointerSize == 0);
    TRACE("Creating VirtualObject id:%d size:%d\n", id, size);
    int num_fields = size / kPointerSize;
    fields_.reserve(num_fields);
    for (int i = 0; i < num_fields; ++i) {
      fields_.push_back(var_states->NewVariable());
    }
  }
  bool FieldAt(int offset, Variable* var) const {
    DCHECK(offset % kPointerSize == 0);
    CHECK(!HasEscaped());
    if (offset >= size()) {
      // This can only happen in unreachable code.
      return false;
    }
    *var = fields_.at(offset / kPointerSize);
    return true;
  }
  Id id() const { return id_; }
  int size() const { return static_cast<int>(kPointerSize * fields_.size()); }
  void SetEscaped() { escaped_ = true; }
  bool HasEscaped() const { return escaped_; }
  const_iterator begin() const { return fields_.begin(); }
  const_iterator end() const { return fields_.end(); }

 private:
  bool escaped_ = false;
  Id id_;
  ZoneVector<Variable> fields_;
};

class EscapeAnalysisState : public EffectGraphReducer {
 public:
  EscapeAnalysisState(JSGraph* jsgraph, Zone* zone)
      : EffectGraphReducer(jsgraph->graph(), zone),
        virtual_objects_(zone),
        replacements_(zone),
        variable_states_(jsgraph, this, zone),
        jsgraph_(jsgraph),
        zone_(zone) {}

  class Scope : public VariableStates::Scope {
   public:
    Scope(EscapeAnalysisState* reducer, Node* node, Reduction* reduction)
        : VariableStates::Scope(&reducer->variable_states_, node, reduction),
          reducer_(reducer) {}

    const VirtualObject* GetVirtualObject(Node* node) {
      VirtualObject* object = reducer_->virtual_objects_[node];
      if (object) object->AddDependency(current_node());
      return object;
    }
    const VirtualObject* InitVirtualObject(int size) {
      VirtualObject* vobject = reducer_->virtual_objects_[current_node()];
      if (vobject) {
        CHECK(vobject->size() == size);
      } else {
        vobject = reducer_->NewVirtualObject(size);
      }
      vobject_ = vobject;
      return vobject;
    }
    void SetVirtualObject(Node* object) {
      vobject_ = reducer_->virtual_objects_[object];
    }

    void SetEscaped(Node* node) {
      if (VirtualObject* object = reducer_->virtual_objects_[node]) {
        if (object->HasEscaped()) return;
        TRACE("Setting %s#%d to escaped because of use by %s#%d\n",
              node->op()->mnemonic(), node->id(),
              current_node()->op()->mnemonic(), current_node()->id());
        object->SetEscaped();
        object->RevisitDependants(reducer_);
      }
    }

    Node* ValueInput(int i) {
      return reducer_->ResolveReplacement(
          NodeProperties::GetValueInput(current_node(), i));
    }
    Node* ContextInput() {
      return reducer_->ResolveReplacement(
          NodeProperties::GetContextInput(current_node()));
    }

    void SetCurrentReplacement(Node* replacement) {
      replacement_ = replacement;
      if (replacement) vobject_ = reducer_->virtual_objects_[replacement];
    }

    ~Scope() {
      if (replacement_ != reducer_->replacements_[current_node()] ||
          vobject_ != reducer_->virtual_objects_[current_node()]) {
        reduction()->set_value_changed();
      }
      reducer_->replacements_[current_node()] = replacement_;
      reducer_->virtual_objects_[current_node()] = vobject_;
    }

   private:
    EscapeAnalysisState* reducer_;
    VirtualObject* vobject_ = nullptr;
    Node* replacement_ = nullptr;
  };

  Node* GetReplacementOf(Node* node) { return replacements_[node]; }
  Node* ResolveReplacement(Node* node) {
    if (Node* repl = GetReplacementOf(node)) return repl;
    return node;
  }

 protected:
  const size_t kMaxTrackedObjects = 100;
  VirtualObject* NewVirtualObject(int size) {
    if (next_object_id_ >= kMaxTrackedObjects) return nullptr;
    return new (zone_)
        VirtualObject(&variable_states_, next_object_id_++, size);
  }
  JSGraph* jsgraph() { return jsgraph_; }

 private:
  friend class NewEscapeAnalysisReducer;
  const VirtualObject* GetVirtualObject(Node* node) {
    return virtual_objects_[node];
  }
  Node* GetVirtualObjectField(const VirtualObject* vobject, int field,
                              Node* effect) {
    Variable var;
    bool success = vobject->FieldAt(field, &var);
    CHECK(success);
    return variable_states_.Get(var, effect);
  }
  Sidetable<VirtualObject*> virtual_objects_;
  Sidetable<Node*> replacements_;
  VariableStates variable_states_;
  VirtualObject::Id next_object_id_ = 0;
  JSGraph* jsgraph_;
  Zone* zone_;

  DISALLOW_COPY_AND_ASSIGN(EscapeAnalysisState);
};

class V8_EXPORT_PRIVATE NewEscapeAnalysis final
    : public NON_EXPORTED_BASE(EscapeAnalysisState) {
 public:
  NewEscapeAnalysis(JSGraph* graph, Zone* zone)
      : EscapeAnalysisState(graph, zone) {}
  void ReduceNode(const Operator* op, EscapeAnalysisState::Scope* current);
  Reduction Reduce(Node* node) override;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NEW_ESCAPE_ANALYSIS_H_
