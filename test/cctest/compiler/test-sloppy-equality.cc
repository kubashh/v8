// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api-inl.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-observer.h"
#include "src/compiler/node.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/simplified-operator.h"
#include "src/handles/handles.h"
#include "src/objects/type-hints.h"
#include "test/cctest/cctest.h"
#include "test/common/wasm/flag-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

class Tester {
 public:
  explicit Tester(Isolate* isolate, const char* script)
      : isolate_(isolate), script_(script) {
    DCHECK_NOT_NULL(isolate_);
    DCHECK_NOT_NULL(script_);
    CompileRun(script_);
  }

  void OptimizeFunctionWithObserver(const char* function_name,
                                    NodeObserver* observer) {
    DCHECK_NOT_NULL(function_name);
    DCHECK_NOT_NULL(observer);
    Local<Function> api_function = Local<Function>::Cast(
        CcTest::global()
            ->Get(CcTest::isolate()->GetCurrentContext(), v8_str(function_name))
            .ToLocalChecked());
    Handle<JSFunction> function =
        Handle<JSFunction>::cast(v8::Utils::OpenHandle(*api_function));
    CHECK(function->shared().HasBytecodeArray());
    Zone zone(isolate_->allocator(), ZONE_NAME);
    Handle<SharedFunctionInfo> sfi(function->shared(), isolate_);
    IsCompiledScope is_compiled_scope(sfi->is_compiled_scope(isolate_));
    JSFunction::EnsureFeedbackVector(function, &is_compiled_scope);

    OptimizedCompilationInfo compilation_info(&zone, isolate_, sfi, function,
                                              CodeKind::TURBOFAN);
    compilation_info.SetNodeObserver(observer);
    v8::internal::CanonicalHandleScope canonical(isolate_);
    compilation_info.ReopenHandlesInNewHandleScope(isolate_);
    Handle<Code> code =
        Pipeline::GenerateCodeForTesting(&compilation_info, isolate_)
            .ToHandleChecked();
    function->set_code(*code);
  }

 private:
  Isolate* isolate_;
  const char* script_;
};

class CreationObserver : public NodeObserver {
 public:
  explicit CreationObserver(std::function<void(const Node*)> handler)
      : handler_(handler) {
    DCHECK(handler_);
  }

  Observation OnNodeCreated(const Node* node) override {
    handler_(node);
    return Observation::kStop;
  }

 private:
  std::function<void(const Node*)> handler_;
};

class ModificationObserver : public NodeObserver {
 public:
  explicit ModificationObserver(
      std::function<void(const Node*)> on_created_handler,
      std::function<void(const Node*, const ObservableNodeState& old_state)>
          on_changed_handler)
      : on_created_handler_(on_created_handler),
        on_changed_handler_(on_changed_handler) {
    DCHECK(on_created_handler_);
    DCHECK(on_changed_handler_);
  }

  Observation OnNodeCreated(const Node* node) override {
    on_created_handler_(node);
    return Observation::kContinue;
  }

  Observation OnNodeChanged(const char* reducer_name, const Node* node,
                            const ObservableNodeState& old_state) override {
    on_changed_handler_(node, old_state);
    return Observation::kContinue;
  }

 private:
  std::function<void(const Node*)> on_created_handler_;
  std::function<void(const Node*, const ObservableNodeState& old_state)>
      on_changed_handler_;
};

class ObserverFactory {
 public:
  explicit ObserverFactory(Zone* zone) : zone_(zone) {}

  NodeObserver* SpeculativeNumberEqual(NumberOperationHint hint) {
    return zone_->New<CreationObserver>(
        CreationObserver([hint](const Node* node) {
          DCHECK_EQ(IrOpcode::kSpeculativeNumberEqual, node->opcode());
          DCHECK_EQ(hint, NumberOperationHintOf(node->op()));
        }));
  }

  NodeObserver* JSEqual(CompareOperationHint hint) {
    return zone_->New<CreationObserver>(
        CreationObserver([/*hint*/](const Node* node) {
          DCHECK_EQ(IrOpcode::kJSEqual, node->opcode());
          // DCHECK_EQ(hint, CompareOperationHintOf(node->op()));
        }));
  }

  NodeObserver* OperatorChange(IrOpcode::Value created_op,
                               IrOpcode::Value modified_op) {
    return zone_->New<ModificationObserver>(ModificationObserver(
        [created_op](const Node* node) {
          DCHECK_EQ(created_op, node->opcode());
        },
        [modified_op](const Node* node, const ObservableNodeState& old_state) {
          if (old_state.opcode() != node->opcode()) {
            DCHECK_EQ(modified_op, node->opcode());
          }
        }));
  }

 private:
  Zone* zone_;
};

struct TestCase {
  TestCase(const char* l, const char* r, NodeObserver* observer)
      : warmup{std::make_pair(l, r)}, observer(observer) {
    DCHECK_NOT_NULL(observer);
  }
  std::vector<std::pair<const char*, const char*>> warmup;
  NodeObserver* observer;
};

TEST(TestSloppyEquality) {
  FlagScope<bool> allow_natives_syntax(&i::FLAG_allow_natives_syntax, true);
  FlagScope<bool> always_opt(&i::FLAG_always_opt, false);
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone zone(isolate->allocator(), ZONE_NAME);
  ObserverFactory f(&zone);
  // TODO(nicohartmann@, v8:5660): Collect more precise feedback for some useful
  // cases.
  TestCase cases[] = {
      {"3", "8", f.SpeculativeNumberEqual(NumberOperationHint::kSignedSmall)},
      {"3", "null", f.JSEqual(CompareOperationHint::kAny)},
      {"3", "undefined", f.JSEqual(CompareOperationHint::kAny)},
      {"3", "true",
       f.SpeculativeNumberEqual(NumberOperationHint::kNumberOrBoolean)},
      {"3", "\"abc\"", f.JSEqual(CompareOperationHint::kAny)},
      {"3.14", "3", f.SpeculativeNumberEqual(NumberOperationHint::kNumber)},
      {"3.14", "null", f.JSEqual(CompareOperationHint::kAny)},
      {"3.14", "undefined", f.JSEqual(CompareOperationHint::kAny)},
      {"3.14", "true",
       f.SpeculativeNumberEqual(NumberOperationHint::kNumberOrBoolean)},
      {"3.14", "\"abc\"", f.JSEqual(CompareOperationHint::kAny)},
      {"\"abc\"", "3", f.JSEqual(CompareOperationHint::kAny)},
      {"\"abc\"", "null", f.JSEqual(CompareOperationHint::kAny)},
      {"\"abc\"", "undefined", f.JSEqual(CompareOperationHint::kAny)},
      {"\"abc\"", "true", f.JSEqual(CompareOperationHint::kAny)},
      {"\"abc\"", "\"xy\"",
       f.JSEqual(CompareOperationHint::kInternalizedString)},
      {"true", "3",
       f.SpeculativeNumberEqual(NumberOperationHint::kNumberOrBoolean)},
      {"true", "null", f.JSEqual(CompareOperationHint::kAny)},
      {"true", "undefined", f.JSEqual(CompareOperationHint::kAny)},
      {"true", "true",
       f.SpeculativeNumberEqual(NumberOperationHint::kNumberOrBoolean)},
      {"true", "\"abc\"", f.JSEqual(CompareOperationHint::kAny)},
      {"undefined", "3", f.JSEqual(CompareOperationHint::kAny)},
      {"undefined", "null",
       f.JSEqual(CompareOperationHint::kReceiverOrNullOrUndefined)},
      {"undefined", "undefined",
       f.JSEqual(CompareOperationHint::kReceiverOrNullOrUndefined)},
      {"undefined", "true", f.JSEqual(CompareOperationHint::kAny)},
      {"undefined", "\"abc\"", f.JSEqual(CompareOperationHint::kAny)},
      {"{}", "3", f.JSEqual(CompareOperationHint::kAny)},
      {"{}", "null",
       f.JSEqual(CompareOperationHint::kReceiverOrNullOrUndefined)},
      {"{}", "undefined",
       f.JSEqual(CompareOperationHint::kReceiverOrNullOrUndefined)},
      {"{}", "true", f.JSEqual(CompareOperationHint::kAny)},
      {"{}", "\"abc\"", f.JSEqual(CompareOperationHint::kAny)},

      {"3.14", "3",
       f.OperatorChange(IrOpcode::kSpeculativeNumberEqual,
                        IrOpcode::kFloat64Equal)}};

  for (const auto& c : cases) {
    std::ostringstream src;
    src << "function test(a, b) {\n"
        << "  return %ObserveNode(a == b);\n"
        << "}\n"
        << "%PrepareFunctionForOptimization(test);\n";
    for (const auto& args : c.warmup) {
      src << "test(" << args.first << ", " << args.second << ");\n"
          << "test(" << args.first << ", " << args.second << ");\n";
    }

    Tester tester(isolate, src.str().c_str());
    tester.OptimizeFunctionWithObserver("test", c.observer);
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
