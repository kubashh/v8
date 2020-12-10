// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CCTEST_COMPILER_NODEOBSERVER_TESTER_H_
#define V8_CCTEST_COMPILER_NODEOBSERVER_TESTER_H_

#include "src/compiler/node-observer.h"
#include "src/compiler/simplified-operator.h"
#include "src/objects/type-hints.h"
#include "test/cctest/cctest.h"
#include "test/common/wasm/flag-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

// The purpose of this class is to provide testing facility for the
// %ObserveNode intrinsic.
class NodeObserverTester : public HandleAndZoneScope {
 public:
  explicit NodeObserverTester(Isolate* isolate, const char* script)
      : isolate_(isolate), script_(script) {
    DCHECK_NOT_NULL(isolate_);
    DCHECK_NOT_NULL(script_);
    CompileRun(script_);
  }

  void OptimizeFunctionWithObserver(const char* function_name,
                                    NodeObserver* observer);

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
    return zone_->New<CreationObserver>([hint](const Node* node) {
      CHECK_EQ(IrOpcode::kSpeculativeNumberEqual, node->opcode());
      CHECK_EQ(hint, NumberOperationHintOf(node->op()));
    });
  }

  NodeObserver* JSEqual(CompareOperationHint /*hint*/) {
    return zone_->New<CreationObserver>([](const Node* node) {
      CHECK_EQ(IrOpcode::kJSEqual, node->opcode());
      // TODO(paolosev): compare hint
    });
  }

  NodeObserver* OperatorChange(IrOpcode::Value created_op,
                               IrOpcode::Value modified_op) {
    return zone_->New<ModificationObserver>(
        [created_op](const Node* node) {
          CHECK_EQ(created_op, node->opcode());
        },
        [modified_op](const Node* node, const ObservableNodeState& old_state) {
          if (old_state.opcode() != node->opcode()) {
            CHECK_EQ(modified_op, node->opcode());
          }
        });
  }

 private:
  Zone* zone_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_CCTEST_COMPILER_NODEOBSERVER_TESTER_H_
