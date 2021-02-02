// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_X64_BASELINE_COMPILER_X64_H_
#define V8_BASELINE_X64_BASELINE_COMPILER_X64_H_

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

void BaselineAssembler::AddSmi(Register lhs, Smi rhs) {
  addl(lhs, Immediate(rhs));
}
void BaselineAssembler::AddSmi(Register lhs, Register rhs) { addl(lhs, rhs); }
void BaselineAssembler::SubSmi(Register lhs, Smi rhs) {
  subl(lhs, Immediate(rhs));
}
void BaselineAssembler::SubSmi(Register lhs, Register rhs) { subl(lhs, rhs); }
void BaselineAssembler::MulSmi(Register lhs, Register rhs) {
  DCHECK_EQ(lhs, rax);
  DCHECK_NE(lhs, rhs);
  SmiUntag(lhs);
  mull(rhs);
}
void BaselineAssembler::BitwiseOrSmi(Register lhs, Smi rhs) {
  orl(lhs, Immediate(rhs));
}
void BaselineAssembler::BitwiseOrSmi(Register lhs, Register rhs) {
  orl(lhs, rhs);
}
void BaselineAssembler::BitwiseXorSmi(Register lhs, Smi rhs) {
  xorl(lhs, Immediate(rhs));
}
void BaselineAssembler::BitwiseXorSmi(Register lhs, Register rhs) {
  xorl(lhs, rhs);
}
void BaselineAssembler::BitwiseAndSmi(Register lhs, Smi rhs) {
  andl(lhs, Immediate(rhs));
}
void BaselineAssembler::BitwiseAndSmi(Register lhs, Register rhs) {
  andl(lhs, rhs);
}
void BaselineAssembler::ShiftLeftSmi(Register lhs, int32_t rhs) {
  shll(lhs, Immediate(rhs));
}
void BaselineAssembler::ShiftRightSmi(Register lhs, int32_t rhs) {
  sarl(lhs, Immediate(rhs));
}
void BaselineAssembler::ShiftRightLogicalSmi(Register lhs, int32_t rhs) {
  shrl(lhs, Immediate(rhs));
}

void BaselineAssembler::Switch(Register reg, int case_value_base,
                               Label** labels, int num_labels) {
  Label fallthrough, jump_table;
  if (case_value_base > 0) {
    subq(reg, Immediate(case_value_base));
  }
  cmpq(reg, Immediate(num_labels));
  j(above_equal, &fallthrough);
  leaq(kScratchRegister, MemOperand(&jump_table));
  jmp(MemOperand(kScratchRegister, reg, times_8, 0));
  // Emit the jump table inline, under the assumption that it's not too big.
  Align(kSystemPointerSize);
  bind(&jump_table);
  for (int i = 0; i < num_labels; ++i) {
    dq(labels[i]);
  }
  bind(&fallthrough);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_X64_BASELINE_COMPILER_X64_H_
