// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-math-builtin-reducer.h"

#include "src/api.h"
#include "src/builtins/builtins-utils.h"
#include "src/code-factory.h"
#include "src/code-stubs.h"
#include "src/compilation-dependencies.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/allocation-builder.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/simplified-operator.h"
#include "src/feedback-vector-inl.h"
#include "src/ic/call-optimization.h"
#include "src/objects-inl.h"
#include "src/vector-slot-pair.h"

namespace v8 {
namespace internal {
namespace compiler {

JSMathBuiltinReducer::JSMathBuiltinReducer(
    AdvancedReducer::Editor* editor, JSGraph* jsgraph,
    Handle<Context> native_context, CompilationDependencies* dependencies)
    : JSBuilderMixin<AdvancedReducerHelper>(jsgraph, editor),
      native_context_(native_context),
      dependencies_(dependencies) {}

Reduction JSMathBuiltinReducer::ReduceMathUnary(Node* node,
                                                const Operator* op) {
  if (node->op()->ValueInputCount() < 3) {
    Node* value = jsgraph()->NaNConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }

  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* input = NodeProperties::GetValueInput(node, 2);

  input = effect = graph()->NewNode(
      simplified()->SpeculativeToNumber(NumberOperationHint::kNumber), input,
      effect, control);
  Node* value = graph()->NewNode(op, input);
  ReplaceWithValue(node, value, effect);
  return Replace(value);
}

Reduction JSMathBuiltinReducer::ReduceMathBinary(Node* node,
                                                 const Operator* op) {
  if (node->op()->ValueInputCount() < 4) {
    Node* value = jsgraph()->NaNConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  Node* left = NodeProperties::GetValueInput(node, 2);
  Node* right = NodeProperties::GetValueInput(node, 3);
  left = effect = graph()->NewNode(
      simplified()->SpeculativeToNumber(NumberOperationHint::kNumberOrOddball),
      left, effect, control);
  right = effect = graph()->NewNode(
      simplified()->SpeculativeToNumber(NumberOperationHint::kNumberOrOddball),
      right, effect, control);
  Node* value = graph()->NewNode(op, left, right);
  ReplaceWithValue(node, value, effect);
  return Replace(value);
}

// ES6 section 20.2.2.19 Math.imul ( x, y )
Reduction JSMathBuiltinReducer::ReduceMathImul(Node* node) {
  if (node->op()->ValueInputCount() < 4) {
    Node* value = jsgraph()->ZeroConstant();
    ReplaceWithValue(node, value);
    return Replace(value);
  }
  Node* left = NodeProperties::GetValueInput(node, 2);
  Node* right = NodeProperties::GetValueInput(node, 3);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  left = effect = graph()->NewNode(
      simplified()->SpeculativeToNumber(NumberOperationHint::kNumberOrOddball),
      left, effect, control);
  right = effect = graph()->NewNode(
      simplified()->SpeculativeToNumber(NumberOperationHint::kNumberOrOddball),
      right, effect, control);
  left = graph()->NewNode(simplified()->NumberToUint32(), left);
  right = graph()->NewNode(simplified()->NumberToUint32(), right);
  Node* value = graph()->NewNode(simplified()->NumberImul(), left, right);
  ReplaceWithValue(node, value, effect);
  return Replace(value);
}

// ES6 section 20.2.2.11 Math.clz32 ( x )
Reduction JSMathBuiltinReducer::ReduceMathClz32(Node* node) {
  if (node->op()->ValueInputCount() < 3) {
    Node* value = jsgraph()->Int32Constant(32);
    ReplaceWithValue(node, value);
    return Replace(value);
  }
  Node* input = NodeProperties::GetValueInput(node, 2);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  input = effect = graph()->NewNode(
      simplified()->SpeculativeToNumber(NumberOperationHint::kNumber), input,
      effect, control);
  input = graph()->NewNode(simplified()->NumberToUint32(), input);
  Node* value = graph()->NewNode(simplified()->NumberClz32(), input);
  ReplaceWithValue(node, value, effect);
  return Replace(value);
}

// ES6 section 20.2.2.24 Math.max ( value1, value2, ...values )
// and
// ES6 section 20.2.2.25 Math.min ( value1, value2, ...values )
Reduction JSMathBuiltinReducer::ReduceMathMinMax(Node* node, const Operator* op,
                                                 Node* empty_value) {
  if (node->op()->ValueInputCount() <= 2) {
    ReplaceWithValue(node, empty_value);
    return Replace(empty_value);
  }
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* value = effect = graph()->NewNode(
      simplified()->SpeculativeToNumber(NumberOperationHint::kNumber),
      NodeProperties::GetValueInput(node, 2), effect, control);
  for (int i = 3; i < node->op()->ValueInputCount(); i++) {
    Node* input = effect = graph()->NewNode(
        simplified()->SpeculativeToNumber(NumberOperationHint::kNumber),
        NodeProperties::GetValueInput(node, i), effect, control);
    value = graph()->NewNode(op, value, input);
  }

  ReplaceWithValue(node, value, effect);
  return Replace(value);
}

template <class Mix>
Graph* JSBuilderMixin<Mix>::graph() const {
  return jsgraph()->graph();
}

template <class Mix>
Isolate* JSBuilderMixin<Mix>::isolate() const {
  return jsgraph()->isolate();
}

template <class Mix>
Factory* JSBuilderMixin<Mix>::factory() const {
  return jsgraph()->factory();
}

template <class Mix>
CommonOperatorBuilder* JSBuilderMixin<Mix>::common() const {
  return jsgraph()->common();
}

template <class Mix>
JSOperatorBuilder* JSBuilderMixin<Mix>::javascript() const {
  return jsgraph()->javascript();
}

template <class Mix>
SimplifiedOperatorBuilder* JSBuilderMixin<Mix>::simplified() const {
  return jsgraph()->simplified();
}

Reduction JSMathBuiltinReducer::ReduceJSCall(
    Node* node, Handle<JSFunction> function,
    Handle<SharedFunctionInfo> shared) {
  switch (shared->code()->builtin_index()) {
    case Builtins::kMathAbs:
      return ReduceMathUnary(node, simplified()->NumberAbs());
    case Builtins::kMathAcos:
      return ReduceMathUnary(node, simplified()->NumberAcos());
    case Builtins::kMathAcosh:
      return ReduceMathUnary(node, simplified()->NumberAcosh());
    case Builtins::kMathAsin:
      return ReduceMathUnary(node, simplified()->NumberAsin());
    case Builtins::kMathAsinh:
      return ReduceMathUnary(node, simplified()->NumberAsinh());
    case Builtins::kMathAtan:
      return ReduceMathUnary(node, simplified()->NumberAtan());
    case Builtins::kMathAtanh:
      return ReduceMathUnary(node, simplified()->NumberAtanh());
    case Builtins::kMathCbrt:
      return ReduceMathUnary(node, simplified()->NumberCbrt());
    case Builtins::kMathCeil:
      return ReduceMathUnary(node, simplified()->NumberCeil());
    case Builtins::kMathCos:
      return ReduceMathUnary(node, simplified()->NumberCos());
    case Builtins::kMathCosh:
      return ReduceMathUnary(node, simplified()->NumberCosh());
    case Builtins::kMathExp:
      return ReduceMathUnary(node, simplified()->NumberExp());
    case Builtins::kMathExpm1:
      return ReduceMathUnary(node, simplified()->NumberExpm1());
    case Builtins::kMathFloor:
      return ReduceMathUnary(node, simplified()->NumberFloor());
    case Builtins::kMathFround:
      return ReduceMathUnary(node, simplified()->NumberFround());
    case Builtins::kMathLog:
      return ReduceMathUnary(node, simplified()->NumberLog());
    case Builtins::kMathLog1p:
      return ReduceMathUnary(node, simplified()->NumberLog1p());
    case Builtins::kMathLog10:
      return ReduceMathUnary(node, simplified()->NumberLog10());
    case Builtins::kMathLog2:
      return ReduceMathUnary(node, simplified()->NumberLog2());
    case Builtins::kMathRound:
      return ReduceMathUnary(node, simplified()->NumberRound());
    case Builtins::kMathSign:
      return ReduceMathUnary(node, simplified()->NumberSign());
    case Builtins::kMathSin:
      return ReduceMathUnary(node, simplified()->NumberSin());
    case Builtins::kMathSinh:
      return ReduceMathUnary(node, simplified()->NumberSinh());
    case Builtins::kMathSqrt:
      return ReduceMathUnary(node, simplified()->NumberSqrt());
    case Builtins::kMathTan:
      return ReduceMathUnary(node, simplified()->NumberTan());
    case Builtins::kMathTanh:
      return ReduceMathUnary(node, simplified()->NumberTanh());
    case Builtins::kMathTrunc:
      return ReduceMathUnary(node, simplified()->NumberTrunc());
    case Builtins::kMathAtan2:
      return ReduceMathBinary(node, simplified()->NumberAtan2());
    case Builtins::kMathPow:
      return ReduceMathBinary(node, simplified()->NumberPow());
    case Builtins::kMathClz32:
      return ReduceMathClz32(node);
    case Builtins::kMathImul:
      return ReduceMathImul(node);
    case Builtins::kMathMax:
      return ReduceMathMinMax(node, simplified()->NumberMax(),
                              jsgraph()->Constant(-V8_INFINITY));
    case Builtins::kMathMin:
      return ReduceMathMinMax(node, simplified()->NumberMin(),
                              jsgraph()->Constant(V8_INFINITY));
    default:
      break;
  }
  return NoChange();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
