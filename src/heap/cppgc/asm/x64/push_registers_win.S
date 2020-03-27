// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

.att_syntax

.type PushAllRegistersAndIterateStack, %function
.global PushAllRegistersAndIterateStack
.hidden PushAllRegistersAndIterateStack

PushAllRegistersAndIterateStack:
    // Push all callee-saved registers to get them on the stack for conservative
    // stack scanning.
    //
    // We maintain 16-byte alignment at calls (required on Mac). There is an
    // 8-byte return address on the stack and we push 56 bytes which maintains
    // 16-byte stack alignment at the call.
    // Source:
    // - https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention?view=vs-2019#callercallee-saved-registers
    // - https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention?view=vs-2019
    push $0x55F00D
    push %rsi
    push %rdi
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    // Pass 1st parameter (rcx) unchanged (this).
    // Pass 2nd parameter (rdx) unchanged (StackVisitor*).
    // Save 3rd parameter (r8; callback)
    mov %r8, %r9
    // Pass 3rd parameter as rsp (stack pointer).
    mov %rsp, %r8
    // Call the callback.
    call *%r9
    // Pop the callee-saved registers. None of them were modified so no
    // restoring is needed.
    add $72, %rsp
    ret
