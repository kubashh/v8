// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_ASYNC_H_
#define V8_BUILTINS_BUILTINS_ASYNC_H_

#include "src/builtins/builtins-promise-gen.h"

namespace v8 {
namespace internal {

// Describe fields of Context associated with AsyncGeneratorAwait resume
// and AsyncFunctionAwait resume closures.
class AwaitContext {
 public:
  enum Fields { kGeneratorSlot = Context::MIN_CONTEXT_SLOTS, kLength };
};

class AsyncBuiltinsAssembler : public PromiseBuiltinsAssembler {
 public:
  explicit AsyncBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : PromiseBuiltinsAssembler(state) {}

 protected:
  TNode<BoolT> TaggedIsGeneratorObject(Node* tagged_object);

  // Perform steps to resume generator after `value` is resolved.
  // `on_reject_context_index` is an index into the Native Context, which should
  // point to a SharedFunctioninfo instance used to create the closure. The
  // value following the reject index should be a similar value for the resolve
  // closure. Returns the Promise-wrapped `value`.
  Node* Await(Node* context, Node* generator, Node* value, Node* outer_promise,
              Node* is_caught, int on_resolve_context_index,
              int on_reject_context_index);
  Node* Await(Node* context, Node* generator, Node* value, Node* outer_promise,
              Node* is_caught, TNode<IntPtrT> on_resolve_context_index,
              TNode<IntPtrT> on_reject_context_index);

  // Return a new built-in function object as defined in
  // Async Iterator Value Unwrap Functions
  Node* CreateUnwrapClosure(Node* const native_context, Node* const done);

  void InitializeNativeClosure(Node* context, Node* native_context,
                               Node* function, Node* shared_info);

 private:
  Node* AllocateAsyncIteratorValueUnwrapContext(Node* native_context,
                                                Node* done);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_ASYNC_H_
