// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_OBJECT_GEN_H_
#define V8_BUILTINS_BUILTINS_OBJECT_GEN_H_

#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

class ObjectBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit ObjectBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  void ReturnToStringFormat(Node* context, Node* string);
  void AddToDictionaryIf(TNode<BoolT> condition,
                         TNode<NameDictionary> name_dictionary,
                         Handle<Name> name, TNode<Object> value,
                         Label* bailout);
  Node* FromPropertyDescriptor(Node* context, Node* desc);
  Node* FromPropertyDetails(Node* context, Node* raw_value, Node* details,
                            Label* if_bailout);
  Node* ConstructAccessorDescriptor(Node* context, Node* getter, Node* setter,
                                    Node* enumerable, Node* configurable);
  Node* ConstructDataDescriptor(Node* context, Node* value, Node* writable,
                                Node* enumerable, Node* configurable);
  Node* GetAccessorOrUndefined(Node* accessor, Label* if_bailout);

  Node* IsSpecialReceiverMap(SloppyTNode<Map> map);

  TNode<Word32T> IsStringWrapperElementsKind(TNode<Map> map);

  void ObjectAssignFast(TNode<Context> context, TNode<JSReceiver> to,
                        TNode<Object> from, Label* slow);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_OBJECT_GEN_H_
