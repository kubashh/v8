// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_INLINING_REDUCER_H_
#define V8_COMPILER_JS_INLINING_REDUCER_H_

#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-graph.h"

namespace v8 {
namespace internal {
namespace compiler {

class JSGraph;
class JSHeapBroker;
class MinimorphicLoadPropertyAccessFeedback;

// This reducer runs during the InliningPhase and reduces JS operators, and is
// thus called "JSInliningReducer". Its scope is not well-defined; the intent
// is for it to contain reductions that don't fit well into other reducers of
// the InliningPhase.
class V8_EXPORT_PRIVATE JSInliningReducer final : public AdvancedReducer {
 public:
  JSInliningReducer(Editor* editor, JSGraph* jsgraph, JSHeapBroker* broker);

  JSInliningReducer(const JSInliningReducer&) = delete;
  JSInliningReducer& operator=(const JSInliningReducer&) = delete;

  const char* reducer_name() const override { return "JSInliningReducer"; }

  inline Reduction Reduce(Node* node) final {
    // Currently we only care about a small set of opcodes here. Check the
    // opcode early to keep overhead low.

    IrOpcode::Value opcode = node->opcode();
    if (opcode != IrOpcode::kJSLoadNamed &&
        opcode != IrOpcode::kJSLoadNamedFromSuper) {
      return NoChange();
    }

    return ReduceImpl(node, opcode);
  }

 private:
  Reduction ReduceImpl(Node* node, IrOpcode::Value opcode);

  Reduction ReduceJSLoadNamed(Node* node);
  Reduction ReduceJSLoadNamedFromSuper(Node* node);

  Reduction ReducePropertyAccess(Node* node,
                                 base::Optional<NameRef> static_name,
                                 FeedbackSource const& source);
  Reduction ReduceMinimorphicPropertyAccess(
      Node* node, MinimorphicLoadPropertyAccessFeedback const& feedback,
      FeedbackSource const& source);

  Graph* graph() const { return jsgraph()->graph(); }
  JSGraph* jsgraph() const { return jsgraph_; }
  JSHeapBroker* broker() const { return broker_; }
  SimplifiedOperatorBuilder* simplified() const {
    return jsgraph()->simplified();
  }

  JSGraph* const jsgraph_;
  JSHeapBroker* const broker_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_INLINING_REDUCER_H_
