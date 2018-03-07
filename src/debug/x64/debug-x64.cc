// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_X64

#include "src/debug/debug.h"

#include "src/assembler.h"
#include "src/debug/liveedit.h"
#include "src/frames-inl.h"
#include "src/macro-assembler.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

void DebugCodegen::GenerateHandleDebuggerStatement(MacroAssembler* masm) {
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kHandleDebuggerStatement, 0);
  }
  __ MaybeDropFrames();

  // Return to caller.
  __ ret(0);
}

void DebugCodegen::GenerateFrameDropperTrampoline(MacroAssembler* masm) {
  // Frame is being dropped:
  // - Drop to the target frame specified by rbx.
  // - Look up current function on the frame.
  // - Leave the frame.
  // - Restart the frame by calling the function.
  __ movp(rbp, rbx);
  __ movp(rdi, Operand(rbp, JavaScriptFrameConstants::kFunctionOffset));
  __ leave();

  __ movp(rbx, FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
  __ movsxlq(
      rbx, FieldOperand(rbx, SharedFunctionInfo::kFormalParameterCountOffset));

  ParameterCount dummy(rbx);
  __ InvokeFunction(rdi, no_reg, dummy, dummy, JUMP_FUNCTION);
}

// void DebugCodegen::GenerateDebugBreakTrampoline(MacroAssembler* masm) {
//  Label skip;
//  // Check whether the flag is set.
//  __ movp(rcx, FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
//  __ movp(rcx, FieldOperand(rcx, SharedFunctionInfo::kDebugInfoOffset));
//  __ JumpIfSmi(rcx, &skip);
//  __ movp(rcx, FieldOperand(rcx, DebugInfo::kFlagsOffset));
//  __ SmiToInteger32(rcx, rcx);
//  __ testp(rcx, Immediate(DebugInfo::kBreakAtEntry));
//  __ j(zero, &skip);
//  {
//    FrameScope scope(masm, StackFrame::MANUAL);
//    // Manually construct frame.
//    __ pushq(rbp);
//    __ movp(rbp, rsp);
//    __ pushq(rsi);
//    __ pushq(rdi);
//    __ pushq(rax);  // Preserve number of arguments.
//    __ pushq(rdx);  // Preserve new target.
//    // Call runtime function with target function as argument.
//    __ pushq(rdi);
//    __ CallRuntime(Runtime::kDebugBreakAtEntry);
//    // Ignore return value.
//    __ popq(rdx);
//    __ popq(rax);
//    __ popq(rdi);
//    __ popq(rsi);
//    // Tear down frame.
//    __ movp(rsp, rbp);
//    __ popq(rbp);
//  }
//  __ bind(&skip);
//  // Tail call code on the shared function info.
//  __ movp(rcx, FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
//  __ movp(rcx, FieldOperand(rcx, SharedFunctionInfo::kCodeOffset));
//  __ leap(rcx, FieldOperand(rcx, Code::kHeaderSize));
//  __ jmp(rcx);
//}

const bool LiveEdit::kFrameDropperSupported = true;

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_X64
