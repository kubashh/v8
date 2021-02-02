// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_IA32_BASELINE_COMPILER_IA32_H_
#define V8_BASELINE_IA32_BASELINE_COMPILER_IA32_H_

#include "src/baseline/baseline-compiler.h"

namespace v8 {
namespace internal {

MemOperand BaselineAssembler::RegisterFrameOperand(
    interpreter::Register interpreter_register) {
  return MemOperand(rbp, interpreter_register.ToOperand() * kSystemPointerSize);
}

void BaselineAssembler::Move(interpreter::Register output, Register source) {
  return movq(RegisterFrameOperand(output), source);
}
void BaselineAssembler::CallBuiltin(Builtins::Name builtin) {
  call(EntryFromBuiltinIndexAsOperand(builtin));
}

void BaselineAssembler::TailCallBuiltin(Builtins::Name builtin) {
  jmp(EntryFromBuiltinIndexAsOperand(builtin));
}

void BaselineAssembler::LoadTaggedPointerField(Register output, Register source,
                                               int offset) {
  MacroAssembler::LoadTaggedPointerField(output, FieldOperand(source, offset));
}
void BaselineAssembler::LoadTaggedSignedField(Register output, Register source,
                                              int offset) {
  MacroAssembler::LoadTaggedSignedField(output, FieldOperand(source, offset));
}
void BaselineAssembler::LoadTaggedAnyField(Register output, Register source,
                                           int offset) {
  LoadAnyTaggedField(output, FieldOperand(source, offset));
}
void BaselineAssembler::StoreTaggedSignedField(Register target, int offset,
                                               Smi value) {
  StoreTaggedField(FieldOperand(target, offset), Immediate(value));
}
void BaselineAssembler::StoreTaggedFieldWithWriteBarrier(Register target,
                                                         int offset,
                                                         Register value,
                                                         Register scratch) {
  DCHECK_NE(target, scratch);
  DCHECK_NE(value, scratch);
  StoreTaggedField(FieldOperand(target, offset), value);
  RecordWriteField(target, offset, value, scratch, kDontSaveFPRegs);
}
void BaselineAssembler::StoreTaggedFieldNoWriteBarrier(Register target,
                                                       int offset,
                                                       Register value) {
  StoreTaggedField(FieldOperand(target, offset), value);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_IA32_BASELINE_COMPILER_IA32_H_
