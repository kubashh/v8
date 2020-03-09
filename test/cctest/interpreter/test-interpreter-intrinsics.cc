// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/init/v8.h"

#include "src/api/api-inl.h"
#include "src/heap/heap-inl.h"
#include "src/interpreter/interpreter-intrinsics.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/interpreter/interpreter-tester.h"

namespace v8 {
namespace internal {
namespace interpreter {

namespace {

class InvokeIntrinsicHelper {
 public:
  InvokeIntrinsicHelper(Isolate* isolate, Zone* zone,
                        Runtime::FunctionId function_id)
      : isolate_(isolate),
        zone_(zone),
        factory_(isolate->factory()),
        function_id_(function_id) {}

  template <class... A>
  Handle<Object> Invoke(A... args) {
    CHECK(IntrinsicsHelper::IsSupported(function_id_));
    int parameter_count = sizeof...(args) + 1;
    BytecodeArrayBuilder builder(zone_, parameter_count, 0, nullptr);
#ifdef V8_REVERSE_JSARGS
    int first_param =
        Register::FromParameterIndex(parameter_count - 1, parameter_count)
            .index();
#else
    int first_param = Register::FromParameterIndex(1, parameter_count).index();
#endif
    RegisterList reg_list =
        InterpreterTester::NewRegisterList(first_param, sizeof...(args));
    builder.CallRuntime(function_id_, reg_list).Return();
    InterpreterTester tester(isolate_, builder.ToBytecodeArray(isolate_));
    auto callable = tester.GetCallable<A...>();
    return callable(args...).ToHandleChecked();
  }

  Handle<Object> NewObject(const char* script) {
    return v8::Utils::OpenHandle(*CompileRun(script));
  }

  Handle<Object> Undefined() { return factory_->undefined_value(); }
  Handle<Object> Null() { return factory_->null_value(); }

 private:
  Isolate* isolate_;
  Zone* zone_;
  Factory* factory_;
  Runtime::FunctionId function_id_;
};

}  // namespace

TEST(IsJSReceiver) {
  HandleAndZoneScope handles;

  InvokeIntrinsicHelper helper(handles.main_isolate(), handles.main_zone(),
                               Runtime::kInlineIsJSReceiver);
  Factory* factory = handles.main_isolate()->factory();

  CHECK_EQ(*factory->true_value(),
           *helper.Invoke(helper.NewObject("new Date()")));
  CHECK_EQ(*factory->true_value(),
           *helper.Invoke(helper.NewObject("(function() {})")));
  CHECK_EQ(*factory->true_value(), *helper.Invoke(helper.NewObject("([1])")));
  CHECK_EQ(*factory->true_value(), *helper.Invoke(helper.NewObject("({})")));
  CHECK_EQ(*factory->true_value(), *helper.Invoke(helper.NewObject("(/x/)")));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.Undefined()));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.Null()));
  CHECK_EQ(*factory->false_value(),
           *helper.Invoke(helper.NewObject("'string'")));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.NewObject("42")));
}

TEST(IsArray) {
  HandleAndZoneScope handles;

  InvokeIntrinsicHelper helper(handles.main_isolate(), handles.main_zone(),
                               Runtime::kInlineIsArray);
  Factory* factory = handles.main_isolate()->factory();

  CHECK_EQ(*factory->false_value(),
           *helper.Invoke(helper.NewObject("new Date()")));
  CHECK_EQ(*factory->false_value(),
           *helper.Invoke(helper.NewObject("(function() {})")));
  CHECK_EQ(*factory->true_value(), *helper.Invoke(helper.NewObject("([1])")));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.NewObject("({})")));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.NewObject("(/x/)")));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.Undefined()));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.Null()));
  CHECK_EQ(*factory->false_value(),
           *helper.Invoke(helper.NewObject("'string'")));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.NewObject("42")));
}

TEST(IsSmi) {
  HandleAndZoneScope handles;

  InvokeIntrinsicHelper helper(handles.main_isolate(), handles.main_zone(),
                               Runtime::kInlineIsSmi);
  Factory* factory = handles.main_isolate()->factory();

  CHECK_EQ(*factory->false_value(),
           *helper.Invoke(helper.NewObject("new Date()")));
  CHECK_EQ(*factory->false_value(),
           *helper.Invoke(helper.NewObject("(function() {})")));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.NewObject("([1])")));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.NewObject("({})")));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.NewObject("(/x/)")));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.Undefined()));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.Null()));
  CHECK_EQ(*factory->false_value(),
           *helper.Invoke(helper.NewObject("'string'")));
  CHECK_EQ(*factory->false_value(), *helper.Invoke(helper.NewObject("42.2")));
  CHECK_EQ(*factory->false_value(),
           *helper.Invoke(helper.NewObject("4294967297")));
  CHECK_EQ(*factory->true_value(), *helper.Invoke(helper.NewObject("42")));
}

TEST(Call) {
  HandleAndZoneScope handles;
  Isolate* isolate = handles.main_isolate();
  Factory* factory = isolate->factory();
  InvokeIntrinsicHelper helper(isolate, handles.main_zone(),
                               Runtime::kInlineCall);

#ifdef V8_REVERSE_JSARGS
  // NOTE: Intrinsics and Runtime functions receiver arguments in the opposite
  // of a JS function.
  CHECK_EQ(Smi::FromInt(20),
           *helper.Invoke(helper.NewObject("({ x: 20 })"),
                          helper.NewObject("(function() { return this.x; })")));
  CHECK_EQ(Smi::FromInt(50),
           *helper.Invoke(
               handle(Smi::FromInt(50), isolate), factory->undefined_value(),
               helper.NewObject("(function(arg1) { return arg1; })")));
  CHECK_EQ(
      Smi::FromInt(20),
      *helper.Invoke(
          handle(Smi::FromInt(3), isolate), handle(Smi::FromInt(7), isolate),
          handle(Smi::FromInt(10), isolate), factory->undefined_value(),
          helper.NewObject("(function(a, b, c) { return a + b + c; })")));
#else   // !V8_REVERSE_JSARGS
  CHECK_EQ(Smi::FromInt(20),
           *helper.Invoke(helper.NewObject("(function() { return this.x; })"),
                          helper.NewObject("({ x: 20 })")));
  CHECK_EQ(Smi::FromInt(50),
           *helper.Invoke(helper.NewObject("(function(arg1) { return arg1; })"),
                          factory->undefined_value(),
                          handle(Smi::FromInt(50), isolate)));
  CHECK_EQ(
      Smi::FromInt(20),
      *helper.Invoke(
          helper.NewObject("(function(a, b, c) { return a + b + c; })"),
          factory->undefined_value(), handle(Smi::FromInt(10), isolate),
          handle(Smi::FromInt(7), isolate), handle(Smi::FromInt(3), isolate)));
#endif  // !V8_REVERSE_JSARGS
}

TEST(IntrinsicAsStubCall) {
  HandleAndZoneScope handles;
  Isolate* isolate = handles.main_isolate();
  Factory* factory = isolate->factory();

  InvokeIntrinsicHelper has_property_helper(isolate, handles.main_zone(),
                                            Runtime::kInlineHasProperty);
#ifdef V8_REVERSE_JSARGS
  // NOTE: Intrinsics and Runtime functions receiver arguments in the opposite
  // of a JS function.
  CHECK_EQ(*factory->true_value(),
           *has_property_helper.Invoke(
               has_property_helper.NewObject("'x'"),
               has_property_helper.NewObject("({ x: 20 })")));
  CHECK_EQ(*factory->false_value(),
           *has_property_helper.Invoke(
               has_property_helper.NewObject("'y'"),
               has_property_helper.NewObject("({ x: 20 })")));
#else   // !V8_REVERSE_JSARGS
  CHECK_EQ(
      *factory->true_value(),
      *has_property_helper.Invoke(has_property_helper.NewObject("({ x: 20 })"),
                                  has_property_helper.NewObject("'x'")));
  CHECK_EQ(
      *factory->false_value(),
      *has_property_helper.Invoke(has_property_helper.NewObject("({ x: 20 })"),
                                  has_property_helper.NewObject("'y'")));
#endif  // !V8_REVERSE_JSA
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
