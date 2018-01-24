// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_MATH_BUILTIN_REDUCER_H_
#define V8_COMPILER_MATH_BUILTIN_REDUCER_H_

#include "src/base/flags.h"
#include "src/compiler/graph-reducer.h"
#include "src/deoptimize-reason.h"

namespace v8 {
namespace internal {

// Forward declarations.
class CompilationDependencies;
class Factory;
class VectorSlotPair;

namespace compiler {

// Forward declarations.
class CallFrequency;
class CommonOperatorBuilder;
class JSGraph;
class JSOperatorBuilder;
class SimplifiedOperatorBuilder;

class AdvancedReducerHelper {
 public:
  explicit AdvancedReducerHelper(AdvancedReducer::Editor* editor)
      : editor_(editor) {}

 protected:
  // Helper functions for subclasses to edit the graph.
  void Replace(Node* node, Node* replacement) {
    DCHECK_NOT_NULL(editor_);
    editor_->Replace(node, replacement);
  }
  void Revisit(Node* node) {
    DCHECK_NOT_NULL(editor_);
    editor_->Revisit(node);
  }
  void ReplaceWithValue(Node* node, Node* value, Node* effect = nullptr,
                        Node* control = nullptr) {
    DCHECK_NOT_NULL(editor_);
    editor_->ReplaceWithValue(node, value, effect, control);
  }

  // Relax the effects of {node} by immediately replacing effect and control
  // uses of {node} with the effect and control input to {node}.
  // TODO(turbofan): replace the effect input to {node} with {graph->start()}.
  void RelaxEffectsAndControls(Node* node) {
    ReplaceWithValue(node, node, nullptr, nullptr);
  }

  // Relax the control uses of {node} by immediately replacing them with the
  // control input to {node}.
  void RelaxControls(Node* node) {
    ReplaceWithValue(node, node, node, nullptr);
  }

  static Reduction NoChange() { return Reduction(); }
  static Reduction Replace(Node* node) { return Reduction(node); }
  static Reduction Changed(Node* node) { return Reduction(node); }

 private:
  AdvancedReducer::Editor* editor_;
};

template <class Mix>
class JSBuilderMixin : public Mix {
 public:
  template <class... A>
  JSBuilderMixin(JSGraph* jsgraph, const A&... mix_params)
      : Mix(mix_params...), jsgraph_(jsgraph) {}

 protected:
  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  Isolate* isolate() const;
  Factory* factory() const;

  CommonOperatorBuilder* common() const;
  JSOperatorBuilder* javascript() const;
  SimplifiedOperatorBuilder* simplified() const;

 private:
  JSGraph* const jsgraph_;
};

class V8_EXPORT_PRIVATE JSMathBuiltinReducer
    : public JSBuilderMixin<AdvancedReducerHelper> {
 public:
  JSMathBuiltinReducer(AdvancedReducer::Editor* editor, JSGraph* jsgraph,
                       Handle<Context> native_context,
                       CompilationDependencies* dependencies);

  Reduction ReduceJSCall(Node* node, Handle<JSFunction> function,
                         Handle<SharedFunctionInfo> shared);

 private:
  Reduction ReduceMathUnary(Node* node, const Operator* op);
  Reduction ReduceMathBinary(Node* node, const Operator* op);
  Reduction ReduceMathImul(Node* node);
  Reduction ReduceMathClz32(Node* node);
  Reduction ReduceMathMinMax(Node* node, const Operator* op, Node* empty_value);

  Handle<Context> native_context() const { return native_context_; }
  CompilationDependencies* dependencies() const { return dependencies_; }
  Handle<JSGlobalProxy> global_proxy() const;

 private:
  Handle<Context> native_context_;
  CompilationDependencies* dependencies_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_MATH_BUILTIN_REDUCER_H_
