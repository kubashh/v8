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

class JSCallReducerTest : public TypedGraphTest {
 public:
  JSCallReducerTest() : javascript_(zone()) {}

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

  Node* MathFunction(const std::string& name) {
    Handle<Object> m =
        JSObject::GetProperty(
            isolate()->global_object(),
            isolate()->factory()->NewStringFromAsciiChecked("Math"))
            .ToHandleChecked();
    Handle<JSFunction> f = Handle<JSFunction>::cast(
        Object::GetProperty(
            m, isolate()->factory()->NewStringFromAsciiChecked(name.c_str()))
            .ToHandleChecked());
    return HeapConstant(f);
  }

  Node* StringFunction(const char* name) {
    Handle<Object> m =
        JSObject::GetProperty(
            isolate()->global_object(),
            isolate()->factory()->NewStringFromAsciiChecked("String"))
            .ToHandleChecked();
    Handle<JSFunction> f = Handle<JSFunction>::cast(
        Object::GetProperty(
            m, isolate()->factory()->NewStringFromAsciiChecked(name))
            .ToHandleChecked());
    return HeapConstant(f);
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

bool JSCallReducerTest::old_flag_lazy_;
bool JSCallReducerTest::old_flag_lazy_handler_;

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

TEST_F(JSCallReducerTest, MathUnaryWithNumber) {
  TRACED_FOREACH(const char*, fnc, kMathUnaries) {
    Node* effect = graph()->start();
    Node* control = graph()->start();
    Node* context = UndefinedConstant();
    Node* frame_state = graph()->start();
    Node* jsfunction = MathFunction(fnc);
    Node* p0 = Parameter(Type::Any(), 0);
    Node* call =
        graph()->NewNode(javascript()->Call(3), jsfunction, UndefinedConstant(),
                         p0, context, frame_state, effect, control);
    Reduction r = Reduce(call);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(std::string(IrOpcode::Mnemonic(r.replacement()->opcode())),
                op_name_for(fnc));
  }
}

// -----------------------------------------------------------------------------
// Math binaries

TEST_F(JSCallReducerTest, MathBinaryWithNumber) {
  TRACED_FOREACH(const char*, fnc, kMathBinaries) {
    Node* jsfunction = MathFunction(fnc);

    Node* effect = graph()->start();
    Node* control = graph()->start();
    Node* context = UndefinedConstant();
    Node* frame_state = graph()->start();
    Node* p0 = Parameter(Type::Any(), 0);
    Node* p1 = Parameter(Type::Any(), 0);
    Node* call =
        graph()->NewNode(javascript()->Call(4), jsfunction, UndefinedConstant(),
                         p0, p1, context, frame_state, effect, control);
    Reduction r = Reduce(call);

    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(std::string(IrOpcode::Mnemonic(r.replacement()->opcode())),
                op_name_for(fnc));
  }
}

// -----------------------------------------------------------------------------
// Math.clz32

TEST_F(JSCallReducerTest, MathClz32WithUnsigned32) {
  Node* jsfunction = MathFunction("clz32");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Unsigned32(), 0);
  Node* call =
      graph()->NewNode(javascript()->Call(3), jsfunction, UndefinedConstant(),
                       p0, context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(std::string(IrOpcode::Mnemonic(r.replacement()->opcode())),
              op_name_for("clz32"));
}

// -----------------------------------------------------------------------------
// Math.imul

TEST_F(JSCallReducerTest, MathImulWithUnsigned32) {
  Node* jsfunction = MathFunction("imul");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Unsigned32(), 0);
  Node* p1 = Parameter(Type::Unsigned32(), 1);
  Node* call =
      graph()->NewNode(javascript()->Call(4), jsfunction, UndefinedConstant(),
                       p0, p1, context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(std::string(IrOpcode::Mnemonic(r.replacement()->opcode())),
              op_name_for("imul"));
}

// -----------------------------------------------------------------------------
// Math.min

TEST_F(JSCallReducerTest, MathMinWithNoArguments) {
  Node* jsfunction = MathFunction("min");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* call =
      graph()->NewNode(javascript()->Call(2), jsfunction, UndefinedConstant(),
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberConstant(V8_INFINITY));
}

TEST_F(JSCallReducerTest, MathMinWithNumber) {
  Node* jsfunction = MathFunction("min");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* call =
      graph()->NewNode(javascript()->Call(3), jsfunction, UndefinedConstant(),
                       p0, context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsSpeculativeToNumber(p0));
}

TEST_F(JSCallReducerTest, MathMinWithTwoArguments) {
  Node* jsfunction = MathFunction("min");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* p1 = Parameter(Type::Any(), 1);
  Node* call =
      graph()->NewNode(javascript()->Call(4), jsfunction, UndefinedConstant(),
                       p0, p1, context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberMin(IsSpeculativeToNumber(p0),
                                           IsSpeculativeToNumber(p1)));
}

// -----------------------------------------------------------------------------
// Math.max

TEST_F(JSCallReducerTest, MathMaxWithNoArguments) {
  Node* jsfunction = MathFunction("max");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* call =
      graph()->NewNode(javascript()->Call(2), jsfunction, UndefinedConstant(),
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberConstant(-V8_INFINITY));
}

TEST_F(JSCallReducerTest, MathMaxWithNumber) {
  Node* jsfunction = MathFunction("max");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* call =
      graph()->NewNode(javascript()->Call(3), jsfunction, UndefinedConstant(),
                       p0, context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsSpeculativeToNumber(p0));
}

TEST_F(JSCallReducerTest, MathMaxWithTwoArguments) {
  Node* jsfunction = MathFunction("max");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* p1 = Parameter(Type::Any(), 1);
  Node* call =
      graph()->NewNode(javascript()->Call(4), jsfunction, UndefinedConstant(),
                       p0, p1, context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberMax(IsSpeculativeToNumber(p0),
                                           IsSpeculativeToNumber(p1)));
}

// -----------------------------------------------------------------------------
// String.fromCharCode

TEST_F(JSCallReducerTest, StringFromCharCodeWithNumber) {
  Node* function = StringFunction("fromCharCode");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* call =
      graph()->NewNode(javascript()->Call(3), function, UndefinedConstant(), p0,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsStringFromCharCode(IsSpeculativeToNumber(p0)));
}

TEST_F(JSCallReducerTest, StringFromCharCodeWithPlainPrimitive) {
  Node* function = StringFunction("fromCharCode");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = graph()->start();
  Node* p0 = Parameter(Type::PlainPrimitive(), 0);
  Node* call =
      graph()->NewNode(javascript()->Call(3), function, UndefinedConstant(), p0,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsStringFromCharCode(IsSpeculativeToNumber(p0)));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
