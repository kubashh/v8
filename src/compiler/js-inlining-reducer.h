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

    // This assert guards the range-check below.
    STATIC_ASSERT(
        IrOpcode::kJSLoadNamed == IrOpcode::kJSLoadProperty + 1 &&
        IrOpcode::kJSLoadNamedFromSuper == IrOpcode::kJSLoadNamed + 1 &&
        IrOpcode::kJSLoadGlobal == IrOpcode::kJSLoadNamedFromSuper + 1 &&
        IrOpcode::kJSStoreProperty == IrOpcode::kJSLoadGlobal + 1 &&
        IrOpcode::kJSStoreNamed == IrOpcode::kJSStoreProperty + 1 &&
        IrOpcode::kJSStoreNamedOwn == IrOpcode::kJSStoreNamed + 1 &&
        IrOpcode::kJSStoreGlobal == IrOpcode::kJSStoreNamedOwn + 1 &&
        IrOpcode::kJSStoreDataPropertyInLiteral ==
            IrOpcode::kJSStoreGlobal + 1 &&
        IrOpcode::kJSStoreInArrayLiteral ==
            IrOpcode::kJSStoreDataPropertyInLiteral + 1 &&
        IrOpcode::kJSDeleteProperty == IrOpcode::kJSStoreInArrayLiteral + 1 &&
        IrOpcode::kJSHasProperty == IrOpcode::kJSDeleteProperty + 1);

    IrOpcode::Value opcode = node->opcode();
    if (opcode < IrOpcode::kJSLoadProperty ||
        opcode > IrOpcode::kJSHasProperty) {
      return NoChange();
    }

    return ReduceImpl(node, opcode);
  }

 private:
  Reduction ReduceImpl(Node* node, IrOpcode::Value opcode);

  Reduction ReduceJSLoadNamed(Node* node);
  Reduction ReduceJSLoadNamedFromSuper(Node* node);
  Reduction ReduceJSStoreNamed(Node* node);
  Reduction ReduceJSStoreNamedOwn(Node* node);
  Reduction ReduceJSHasProperty(Node* node);
  Reduction ReduceJSLoadProperty(Node* node);
  Reduction ReduceJSStoreProperty(Node* node);
  Reduction ReduceJSStoreDataPropertyInLiteral(Node* node);
  Reduction ReduceJSStoreInArrayLiteral(Node* node);

  Reduction ReducePropertyAccess(Node* node, Node* key,
                                 base::Optional<NameRef> static_name,
                                 Node* value, FeedbackSource const& source,
                                 AccessMode access_mode);
  Reduction ReduceMinimorphicPropertyAccess(
      Node* node, Node* value,
      MinimorphicLoadPropertyAccessFeedback const& feedback,
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
