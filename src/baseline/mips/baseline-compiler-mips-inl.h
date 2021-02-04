// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_MIPS_BASELINE_COMPILER_MIPS_INL_H_
#define V8_BASELINE_MIPS_BASELINE_COMPILER_MIPS_INL_H_

#include "src/baseline/baseline-compiler.h"

namespace v8 {
namespace internal {
namespace baseline {

class BaselineAssembler::ScratchRegisterScope {
 public:
  explicit ScratchRegisterScope(BaselineAssembler* assembler)
      : assembler_(assembler), prev_scope_(assembler->scratch_register_scope_) {
    assembler_->scratch_register_scope_ = this;
  }
  ~ScratchRegisterScope() { assembler_->scratch_register_scope_ = prev_scope_; }

  Register AcquireScratch() { UNREACHABLE(); }

 private:
  BaselineAssembler* assembler_;
  ScratchRegisterScope* prev_scope_;
};

// TODO(leszeks): Unify condition names in the MacroAssembler.
enum class Condition : uint8_t {
  // TODO(mips): Make these match conditions in the mips macro-assembler.

  kEqual,
  kNotEqual,

  kLessThan,
  kGreaterThan,
  kLessThanEqual,
  kGreaterThanEqual,

  kUnsignedLessThan,
  kUnsignedGreaterThan,
  kUnsignedLessThanEqual,
  kUnsignedGreaterThanEqual,

  kOverflow,
  kNoOverflow,

  kZero,
  kNotZero,
};

internal::Condition AsMasmCondition(Condition cond) { UNREACHABLE(); }

namespace {

#define __ masm_->

#ifdef DEBUG
bool Clobbers(Register target, MemOperand op) { UNREACHABLE(); }
#endif

}  // namespace

MemOperand BaselineAssembler::RegisterFrameOperand(
    interpreter::Register interpreter_register) {
  UNREACHABLE();
}

void BaselineAssembler::Jump(Label* target, Label::Distance distance) {
  UNREACHABLE();
}
void BaselineAssembler::JumpIf(Condition cc, Label* target,
                               Label::Distance distance) {
  UNREACHABLE();
}
void BaselineAssembler::JumpIfRoot(Register value, RootIndex index,
                                   Label* target, Label::Distance distance) {
  UNREACHABLE();
}
void BaselineAssembler::JumpIfNotRoot(Register value, RootIndex index,
                                      Label* target, Label::Distance distance) {
  UNREACHABLE();
}
void BaselineAssembler::JumpIfSmi(Register value, Label* target,
                                  Label::Distance distance) {
  UNREACHABLE();
}
void BaselineAssembler::JumpIfNotSmi(Register value, Label* target,
                                     Label::Distance distance) {
  UNREACHABLE();
}

void BaselineAssembler::CallBuiltin(Builtins::Name builtin) { UNREACHABLE(); }

void BaselineAssembler::TailCallBuiltin(Builtins::Name builtin) {
  UNREACHABLE();
}
void BaselineAssembler::Test(Register value, int mask) { UNREACHABLE(); }

void BaselineAssembler::CmpObjectType(Register object,
                                      InstanceType instance_type,
                                      Register map) {
  UNREACHABLE();
}
void BaselineAssembler::CmpInstanceType(Register value,
                                        InstanceType instance_type) {
  UNREACHABLE();
}
void BaselineAssembler::Cmp(Register value, Smi smi) { UNREACHABLE(); }
void BaselineAssembler::ComparePointer(Register value, MemOperand operand) {
  UNREACHABLE();
}
void BaselineAssembler::SmiCompare(Register lhs, Register rhs) {
  UNREACHABLE();
}
// cmp_tagged
void BaselineAssembler::CompareTagged(Register value, MemOperand operand) {
  UNREACHABLE();
}
void BaselineAssembler::CompareTagged(MemOperand operand, Register value) {
  UNREACHABLE();
}
void BaselineAssembler::CompareByte(Register value, int32_t byte) {
  UNREACHABLE();
}

void BaselineAssembler::Move(interpreter::Register output, Register source) {
  UNREACHABLE();
}
void BaselineAssembler::Move(Register output, TaggedIndex value) {
  UNREACHABLE();
}
void BaselineAssembler::Move(MemOperand output, Register source) {
  UNREACHABLE();
}
void BaselineAssembler::Move(Register output, ExternalReference reference) {
  UNREACHABLE();
}
void BaselineAssembler::Move(Register output, Handle<HeapObject> value) {
  UNREACHABLE();
}
void BaselineAssembler::Move(Register output, int32_t value) { UNREACHABLE(); }
void BaselineAssembler::MoveMaybeSmi(Register output, Register source) {
  UNREACHABLE();
}
void BaselineAssembler::MoveSmi(Register output, Register source) {
  UNREACHABLE();
}

template <typename... T>
int BaselineAssembler::Push(T... vals) {
  UNREACHABLE();
}

template <typename... T>
void BaselineAssembler::PushReverse(T... vals) {
  UNREACHABLE();
}

template <typename... T>
void BaselineAssembler::Pop(T... registers) {
  UNREACHABLE();
}

void BaselineAssembler::LoadTaggedPointerField(Register output, Register source,
                                               int offset) {
  UNREACHABLE();
}
void BaselineAssembler::LoadTaggedSignedField(Register output, Register source,
                                              int offset) {
  UNREACHABLE();
}
void BaselineAssembler::LoadTaggedAnyField(Register output, Register source,
                                           int offset) {
  UNREACHABLE();
}
void BaselineAssembler::LoadByteField(Register output, Register source,
                                      int offset) {
  UNREACHABLE();
}
void BaselineAssembler::StoreTaggedSignedField(Register target, int offset,
                                               Smi value) {
  UNREACHABLE();
}
void BaselineAssembler::StoreTaggedFieldWithWriteBarrier(Register target,
                                                         int offset,

                                                         Register value) {
  UNREACHABLE();
}
void BaselineAssembler::StoreTaggedFieldNoWriteBarrier(Register target,
                                                       int offset,
                                                       Register value) {
  UNREACHABLE();
}

void BaselineAssembler::AddToInterruptBudget(int32_t weight) { UNREACHABLE(); }

void BaselineAssembler::AddToInterruptBudget(Register weight) { UNREACHABLE(); }

void BaselineAssembler::AddSmi(Register lhs, Smi rhs) { UNREACHABLE(); }

void BaselineAssembler::Switch(Register reg, int case_value_base,
                               Label** labels, int num_labels) {
  UNREACHABLE();
}

#undef __

#define __ basm_.

void BaselineCompiler::Prologue() { UNREACHABLE(); }

void BaselineCompiler::PrologueFillFrame() { UNREACHABLE(); }

void BaselineCompiler::VerifyFrameSize() { UNREACHABLE(); }

#undef __

#define __ basm.

void BaselineAssembler::EmitReturn(MacroAssembler* masm) {
  BaselineAssembler basm(masm);
  // TODO(mips): Implement.
  __ Trap();
}

#undef __

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_MIPS_BASELINE_COMPILER_MIPS_INL_H_
