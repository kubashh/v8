// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_H_
#define V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_H_

#include "src/baseline/baseline-compiler.h"

namespace v8 {
namespace internal {

MemOperand BaselineAssembler::RegisterFrameOperand(
    interpreter::Register interpreter_register) {
  return MemOperand(sp, interpreter_register.ToOperand() * kSystemPointerSize);
}

void BaselineAssembler::Move(interpreter::Register output, Register source) {
  return Str(source, RegisterFrameOperand(output));
}
void BaselineAssembler::CallBuiltin(Builtins::Name builtin) {
  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();
  LoadEntryFromBuiltinIndex(builtin, temp);
  Call(temp);
}

void BaselineAssembler::TailCallBuiltin(Builtins::Name builtin) {
  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();
  LoadEntryFromBuiltinIndex(builtin, temp);
  Jump(temp);
}

void BaselineAssembler::LoadTaggedPointerField(Register output, Register source,
                                               int offset) {
  MacroAssembler::LoadTaggedPointerField(output,
                                         FieldMemOperand(source, offset));
}
void BaselineAssembler::LoadTaggedSignedField(Register output, Register source,
                                              int offset) {
  MacroAssembler::LoadTaggedSignedField(output,
                                        FieldMemOperand(source, offset));
}
void BaselineAssembler::LoadTaggedAnyField(Register output, Register source,
                                           int offset) {
  LoadAnyTaggedField(output, FieldMemOperand(source, offset));
}
void BaselineAssembler::StoreTaggedSignedField(Register target, int offset,
                                               Smi value) {
  UseScratchRegisterScope temps(this);
  Register tmp = temps.AcquireX();
  Mov(tmp, Operand(value));
  StoreTaggedField(tmp, FieldMemOperand(target, offset));
}
void BaselineAssembler::StoreTaggedFieldWithWriteBarrier(Register target,
                                                         int offset,
                                                         Register value,
                                                         Register scratch) {
  DCHECK_NE(target, scratch);
  DCHECK_NE(value, scratch);
  StoreTaggedField(value, FieldMemOperand(target, offset));
  RecordWriteField(target, offset, value, kLRHasNotBeenSaved, kDontSaveFPRegs);
}
void BaselineAssembler::StoreTaggedFieldNoWriteBarrier(Register target,
                                                       int offset,
                                                       Register value) {
  StoreTaggedField(value, FieldMemOperand(target, offset));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_H_
