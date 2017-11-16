// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/handles-inl.h"

#include "src/code-stub-assembler.h"
#include "src/globals.h"
#include "src/macro-assembler.h"

namespace v8 {
namespace internal {

void Builtins::Generate_InterpreterPushArgsThenCall(MacroAssembler* masm) {
  return Generate_InterpreterPushArgsThenCallImpl(
      masm, ConvertReceiverMode::kAny, InterpreterPushArgsMode::kOther);
}

void Builtins::Generate_InterpreterPushArgsThenCallFunction(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsThenCallImpl(
      masm, ConvertReceiverMode::kAny, InterpreterPushArgsMode::kJSFunction);
}

void Builtins::Generate_InterpreterPushUndefinedAndArgsThenCall(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsThenCallImpl(
      masm, ConvertReceiverMode::kNullOrUndefined,
      InterpreterPushArgsMode::kOther);
}

void Builtins::Generate_InterpreterPushUndefinedAndArgsThenCallFunction(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsThenCallImpl(
      masm, ConvertReceiverMode::kNullOrUndefined,
      InterpreterPushArgsMode::kJSFunction);
}

void Builtins::Generate_InterpreterPushArgsThenCallWithFinalSpread(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsThenCallImpl(
      masm, ConvertReceiverMode::kAny,
      InterpreterPushArgsMode::kWithFinalSpread);
}

void Builtins::Generate_InterpreterPushArgsThenConstruct(MacroAssembler* masm) {
  return Generate_InterpreterPushArgsThenConstructImpl(
      masm, InterpreterPushArgsMode::kOther);
}

void Builtins::Generate_InterpreterPushArgsThenConstructWithFinalSpread(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsThenConstructImpl(
      masm, InterpreterPushArgsMode::kWithFinalSpread);
}

void Builtins::Generate_InterpreterPushArgsThenConstructFunction(
    MacroAssembler* masm) {
  return Generate_InterpreterPushArgsThenConstructImpl(
      masm, InterpreterPushArgsMode::kJSFunction);
}

TF_BUILTIN(InterpreterFirstEntryTrampoline, CodeStubAssembler) {
  Node* target_function = LoadFromFrame(StandardFrameConstants::kFunctionOffset,
                                        MachineType::TaggedPointer());
  CSA_ASSERT(this, IsJSFunction(target_function));
  Node* sfi =
      LoadObjectField(target_function, JSFunction::kSharedFunctionInfoOffset);
  CSA_ASSERT(this, IsSharedFunctionInfo(sfi));
  Node* context = Parameter(BuiltinDescriptor::kContext);
  // Jump to the runtime to handle first-execution logging.
  CallRuntime(Runtime::kFunctionFirstExecution, context, sfi);
  // Install the InterpreterEntryTrampolin.
  Handle<Code> code(
      isolate()->builtins()->builtin(Builtins::kInterpreterEntryTrampoline));
  Node* interpreter_entry_trampoline = HeapConstant(code);
  StoreObjectField(target_function, JSFunction::kCodeOffset,
                   interpreter_entry_trampoline);
  // Tail call the InterpreterEntryTrampoline.
  Node* argc = Parameter(BuiltinDescriptor::kArgumentsCount);
  Node* new_target = Parameter(BuiltinDescriptor::kNewTarget);
  BuiltinDescriptor descriptor(isolate());
  TailCallStub(descriptor, interpreter_entry_trampoline, context,
               target_function, new_target, argc);
}

}  // namespace internal
}  // namespace v8
