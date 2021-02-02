// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_H_
#define V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_H_

#include "src/baseline/baseline-compiler.h"

namespace v8 {
namespace internal {

namespace {

#ifdef DEBUG
bool Clobbers(Register target, MemOperand op) {
  return op.base() == target || op.regoffset() == target;
}
#endif

}  // namespace

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

void BaselineAssembler::AddSmi(Register lhs, Smi rhs) {
  Add(lhs.W(), lhs.W(), Immediate(rhs));
}
void BaselineAssembler::AddSmi(Register lhs, Register rhs) {
  Add(lhs.W(), lhs.W(), rhs.W());
}
void BaselineAssembler::SubSmi(Register lhs, Smi rhs) {
  Sub(lhs.W(), lhs.W(), Immediate(rhs));
}
void BaselineAssembler::SubSmi(Register lhs, Register rhs) {
  Sub(lhs.W(), lhs.W(), rhs);
}
void BaselineAssembler::MulSmi(Register lhs, Register rhs) {
  SmiUntag(rhs);
  Mul(lhs.W(), lhs.W(), rhs.W());
}
void BaselineAssembler::BitwiseOrSmi(Register lhs, Smi rhs) {
  Orr(lhs.W(), lhs.W(), Immediate(rhs));
}
void BaselineAssembler::BitwiseOrSmi(Register lhs, Register rhs) {
  Orr(lhs.W(), lhs.W(), rhs);
}
void BaselineAssembler::BitwiseXorSmi(Register lhs, Smi rhs) {
  Eor(lhs.W(), lhs.W(), Immediate(rhs));
}
void BaselineAssembler::BitwiseXorSmi(Register lhs, Register rhs) {
  Eor(lhs.W(), lhs.W(), rhs);
}
void BaselineAssembler::BitwiseAndSmi(Register lhs, Smi rhs) {
  And(lhs.W(), lhs.W(), Immediate(rhs));
}
void BaselineAssembler::BitwiseAndSmi(Register lhs, Register rhs) {
  And(lhs.W(), lhs.W(), rhs);
}
void BaselineAssembler::ShiftLeftSmi(Register lhs, int32_t rhs) {
  Lsl(lhs.W(), lhs.W(), rhs);
}
void BaselineAssembler::ShiftRightSmi(Register lhs, int32_t rhs) {
  Asr(lhs.W(), lhs.W(), rhs);
}
void BaselineAssembler::ShiftRightLogicalSmi(Register lhs, int32_t rhs) {
  Lsr(lhs.W(), lhs.W(), rhs);
}

void BaselineAssembler::Switch(Register reg, int case_value_base,
                               Label** labels, int num_labels) {
  Label fallthrough;
  if (case_value_base > 0) {
    Sub(reg, reg, Immediate(case_value_base));
  }

  // Mostly copied from code-generator-arm64.cc
  UseScratchRegisterScope scope(this);
  Register temp = scope.AcquireX();
  Label table;
  Cmp(reg, num_labels);
  B(hs, &fallthrough);
  Adr(temp, &table);
  int entry_size_log2 = 2;
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  ++entry_size_log2;  // Account for BTI.
#endif
  Add(temp, temp, Operand(reg, UXTW, entry_size_log2));
  Br(temp);
  {
    TurboAssembler::BlockPoolsScope block_pools(this, num_labels * kInstrSize);
    Bind(&table);
    for (int i = 0; i < num_labels; ++i) {
      JumpTarget();
      B(labels[i]);
    }
    JumpTarget();
    Bind(&fallthrough);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_H_
