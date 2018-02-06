// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cctype>

#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-call-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/typer.h"
#include "src/isolate-inl.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"
#include "testing/gmock-support.h"

using testing::BitEq;
using testing::Capture;

namespace v8 {
namespace internal {
namespace compiler {

class JSMathBuiltinReducerTest : public TypedGraphTest {
 public:
  JSMathBuiltinReducerTest() : javascript_(zone()) {}

 protected:
  Reduction Reduce(Node* node) {
    MachineOperatorBuilder machine(zone(), MachineType::PointerRepresentation(),
                                   MachineOperatorBuilder::Flag::kNoFlags);
    SimplifiedOperatorBuilder simplified(zone());
    JSGraph jsgraph(isolate(), graph(), common(), javascript(), &simplified,
                    &machine);
    // TODO(titzer): mock the GraphReducer here for better unit testing.
    GraphReducer graph_reducer(zone(), graph());

    JSCallReducer reducer(&graph_reducer, &jsgraph, JSCallReducer::kNoFlags,
                          native_context(), nullptr);
    return reducer.Reduce(node);
  }

  static void SetUpTestCase() {
    old_flag_lazy_ = i::FLAG_lazy_deserialization;
    old_flag_lazy_handler_ = i::FLAG_lazy_handler_deserialization;
    i::FLAG_lazy_deserialization = false;
    i::FLAG_lazy_handler_deserialization = false;
    TypedGraphTest::SetUpTestCase();
  }

  static void TearDownTestCase() {
    TypedGraphTest::TearDownTestCase();
    i::FLAG_lazy_deserialization = old_flag_lazy_;
    i::FLAG_lazy_handler_deserialization = old_flag_lazy_handler_;
  }

  struct JSFunctionNode {
    Node* node_;
    Handle<JSFunction> function_;
  };

  JSFunctionNode MathFunction(const std::string& name) {
    Handle<Object> m =
        JSObject::GetProperty(
            isolate()->global_object(),
            isolate()->factory()->NewStringFromAsciiChecked("Math"))
            .ToHandleChecked();
    Handle<JSFunction> f = Handle<JSFunction>::cast(
        Object::GetProperty(
            m, isolate()->factory()->NewStringFromAsciiChecked(name.c_str()))
            .ToHandleChecked());
    return JSFunctionNode{HeapConstant(f), f};
  }
  JSOperatorBuilder* javascript() { return &javascript_; }

  std::string op_name_for(const char* fnc) {
    std::string string_fnc(fnc);
    char initial = std::toupper(fnc[0]);
    return std::string("Number") + initial +
           string_fnc.substr(1, std::string::npos);
  }

 private:
  JSOperatorBuilder javascript_;
  static bool old_flag_lazy_;
  static bool old_flag_lazy_handler_;
};

bool JSMathBuiltinReducerTest::old_flag_lazy_;
bool JSMathBuiltinReducerTest::old_flag_lazy_handler_;

namespace {

const char* kMathUnaries[] = {
    "abs",  "acos",  "acosh", "asin", "asinh", "atan",  "cbrt",
    "ceil", "cos",   "cosh",  "exp",  "expm1", "floor", "fround",
    "log",  "log1p", "log10", "log2", "round", "sign",  "sin",
    "sinh", "sqrt",  "tan",   "tanh", "trunc"};

const char* kMathBinaries[] = {"atan2", "pow"};

}  // namespace

// -----------------------------------------------------------------------------
// Math unaries

TEST_F(JSMathBuiltinReducerTest, MathUnaryWithNumber) {
  TRACED_FOREACH(const char*, fnc, kMathUnaries) {
    Node* effect = graph()->start();
    Node* control = graph()->start();
    Node* context = UndefinedConstant();
    Node* frame_state = graph()->start();
    JSFunctionNode jsfunction = MathFunction(fnc);
    Node* p0 = Parameter(Type::Any(), 0);
    Node* call = graph()->NewNode(javascript()->Call(3), jsfunction.node_,
                                  UndefinedConstant(), p0, context, frame_state,
                                  effect, control);
    Reduction r = Reduce(call);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(std::string(IrOpcode::Mnemonic(r.replacement()->opcode())),
                op_name_for(fnc));
  }
}

// -----------------------------------------------------------------------------
// Math binaries

TEST_F(JSMathBuiltinReducerTest, MathBinaryWithNumber) {
  TRACED_FOREACH(const char*, fnc, kMathBinaries) {
    JSFunctionNode jsfunction = MathFunction(fnc);

    Node* effect = graph()->start();
    Node* control = graph()->start();
    Node* context = UndefinedConstant();
    Node* frame_state = graph()->start();
    Node* p0 = Parameter(Type::Any(), 0);
    Node* p1 = Parameter(Type::Any(), 0);
    Node* call = graph()->NewNode(javascript()->Call(4), jsfunction.node_,
                                  UndefinedConstant(), p0, p1, context,
                                  frame_state, effect, control);
    Reduction r = Reduce(call);

    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(std::string(IrOpcode::Mnemonic(r.replacement()->opcode())),
                op_name_for(fnc));
  }
}

// -----------------------------------------------------------------------------
// Math.clz32

TEST_F(JSMathBuiltinReducerTest, MathClz32WithUnsigned32) {
  JSFunctionNode jsfunction = MathFunction("clz32");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Unsigned32(), 0);
  Node* call = graph()->NewNode(javascript()->Call(3), jsfunction.node_,
                                UndefinedConstant(), p0, context, frame_state,
                                effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(std::string(IrOpcode::Mnemonic(r.replacement()->opcode())),
              op_name_for("clz32"));
}

// -----------------------------------------------------------------------------
// Math.imul

TEST_F(JSMathBuiltinReducerTest, MathImulWithUnsigned32) {
  JSFunctionNode jsfunction = MathFunction("imul");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Unsigned32(), 0);
  Node* p1 = Parameter(Type::Unsigned32(), 1);
  Node* call = graph()->NewNode(javascript()->Call(4), jsfunction.node_,
                                UndefinedConstant(), p0, p1, context,
                                frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(std::string(IrOpcode::Mnemonic(r.replacement()->opcode())),
              op_name_for("imul"));
}

// -----------------------------------------------------------------------------
// Math.min

TEST_F(JSMathBuiltinReducerTest, MathMinWithNoArguments) {
  JSFunctionNode jsfunction = MathFunction("min");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* call = graph()->NewNode(javascript()->Call(2), jsfunction.node_,
                                UndefinedConstant(), context, frame_state,
                                effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberConstant(V8_INFINITY));
}

TEST_F(JSMathBuiltinReducerTest, MathMinWithNumber) {
  JSFunctionNode jsfunction = MathFunction("min");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* call = graph()->NewNode(javascript()->Call(3), jsfunction.node_,
                                UndefinedConstant(), p0, context, frame_state,
                                effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsSpeculativeToNumber(p0));
}

TEST_F(JSMathBuiltinReducerTest, MathMinWithTwoArguments) {
  JSFunctionNode jsfunction = MathFunction("min");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* p1 = Parameter(Type::Any(), 1);
  Node* call = graph()->NewNode(javascript()->Call(4), jsfunction.node_,
                                UndefinedConstant(), p0, p1, context,
                                frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberMin(IsSpeculativeToNumber(p0),
                                           IsSpeculativeToNumber(p1)));
}

// -----------------------------------------------------------------------------
// Math.max

TEST_F(JSMathBuiltinReducerTest, MathMaxWithNoArguments) {
  JSFunctionNode jsfunction = MathFunction("max");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* call = graph()->NewNode(javascript()->Call(2), jsfunction.node_,
                                UndefinedConstant(), context, frame_state,
                                effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberConstant(-V8_INFINITY));
}

TEST_F(JSMathBuiltinReducerTest, MathMaxWithNumber) {
  JSFunctionNode jsfunction = MathFunction("max");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* call = graph()->NewNode(javascript()->Call(3), jsfunction.node_,
                                UndefinedConstant(), p0, context, frame_state,
                                effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsSpeculativeToNumber(p0));
}

TEST_F(JSMathBuiltinReducerTest, MathMaxWithTwoArguments) {
  JSFunctionNode jsfunction = MathFunction("max");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* p1 = Parameter(Type::Any(), 1);
  Node* call = graph()->NewNode(javascript()->Call(4), jsfunction.node_,
                                UndefinedConstant(), p0, p1, context,
                                frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberMax(IsSpeculativeToNumber(p0),
                                           IsSpeculativeToNumber(p1)));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
