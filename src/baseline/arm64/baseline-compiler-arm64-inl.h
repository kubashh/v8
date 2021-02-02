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

#define __ masm_.

void BaselineCompiler::Prologue() {
  __ Mov(kInterpreterBytecodeArrayRegister, Operand(bytecode_));
  DCHECK_EQ(kJSFunctionRegister, kJavaScriptCallTargetRegister);
  CallBuiltin(Builtins::kBaselinePrologue, kContextRegister,
              kJSFunctionRegister, kJavaScriptCallArgCountRegister,
              kInterpreterBytecodeArrayRegister);

  FrameScope frame_scope(&masm_, StackFrame::MANUAL);
  __ Push<TurboAssembler::kSignLR>(lr, fp);
  __ mov(fp, sp);

  // BaselinePrologue has already pushed everything on the stack,
  // just increase the SP by stackframe size and continue
  __ Sub(sp.W(), sp.W(), Immediate(kSystemPointerSize * 5));

  __ AssertSpAligned();

  // Stack check. This folds the checks for both the interrupt stack limit
  // check and the real stack limit into one by just checking for the
  // interrupt limit. The interrupt limit is either equal to the real stack
  // limit or tighter. By ensuring we have space until that limit after
  // building the frame we can quickly precheck both at once.
  __ Sub(x10, sp, Immediate(bytecode_->frame_size()));
  {
    UseScratchRegisterScope temps(&masm_);
    Register scratch = temps.AcquireX();
    __ LoadStackLimit(scratch, StackLimitKind::kInterruptStackLimit);
    __ Cmp(x10, scratch);
  }
  Label after_stack_check;
  __ B(hs, &after_stack_check);

  __ RecordComment("[ Stack/interrupt check");
  // Save incoming new target or generator
  __ Push(padreg, kJavaScriptCallNewTargetRegister);
  __ CallRuntime(Runtime::kStackGuard);
  AddAsStartPosition();
  __ Pop(kJavaScriptCallNewTargetRegister, padreg);
  __ RecordComment("]");

  __ bind(&after_stack_check);

  __ AssertSpAligned();
  PrologueFillFrame();
  __ AssertSpAligned();
}

void BaselineCompiler::PrologueFillFrame() {
  __ RecordComment("[ Fill frame");
  // Inlined register frame fill
  interpreter::Register new_target_or_generator_register =
      bytecode_->incoming_new_target_or_generator_register();
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  int register_count = bytecode_->register_count();
  // Magic value
  const int kLoopUnrollSize = 8;
  const int new_target_index = new_target_or_generator_register.index();
  const bool has_new_target = new_target_index != kMaxInt;
  int i = 0;
  if (has_new_target) {
    DCHECK_LE(new_target_index, register_count);
    for (; i + 2 <= new_target_index; i += 2) {
      __ Push(kInterpreterAccumulatorRegister, kInterpreterAccumulatorRegister);
    }
    if (i == new_target_index) {
      __ Push(kJavaScriptCallNewTargetRegister,
              kInterpreterAccumulatorRegister);
    } else {
      __ Push(kInterpreterAccumulatorRegister,
              kJavaScriptCallNewTargetRegister);
    }
    i += 2;
  }
  if (register_count < 2 * kLoopUnrollSize) {
    // If the frame is small enough, just unroll the frame fill completely.
    for (; i < register_count; i += 2) {
      __ Push(kInterpreterAccumulatorRegister, kInterpreterAccumulatorRegister);
    }
  } else {
    UseScratchRegisterScope temps(&masm_);
    Register scratch = temps.AcquireX();

    register_count -= i;
    i = 0;
    // Extract the first few registers to round to the unroll size.
    int first_registers = register_count % kLoopUnrollSize;
    for (; i < first_registers; i += 2) {
      __ Push(kInterpreterAccumulatorRegister, kInterpreterAccumulatorRegister);
    }
    __ Mov(scratch, Immediate(register_count / kLoopUnrollSize));
    Label loop;
    __ bind(&loop);
    for (int j = 0; j < kLoopUnrollSize; j += 2) {
      __ Push(kInterpreterAccumulatorRegister, kInterpreterAccumulatorRegister);
    }
    __ Subs(scratch, scratch, 2);
    __ B(gt, &loop);
  }
  __ RecordComment("]");
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_ARM64_BASELINE_COMPILER_ARM64_H_
