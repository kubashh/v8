// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_ARM64_MAGLEV_ASSEMBLER_ARM64_INL_H_
#define V8_MAGLEV_ARM64_MAGLEV_ASSEMBLER_ARM64_INL_H_

#include "src/codegen/macro-assembler-inl.h"
#include "src/maglev/maglev-assembler.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-code-gen-state.h"

namespace v8 {
namespace internal {
namespace maglev {

constexpr Register kScratchRegister = x16;
constexpr Register kScratchRegisterW = w16;
constexpr DoubleRegister kScratchDoubleReg = d30;

namespace detail {

template <typename Arg>
inline Register ToRegister(MaglevAssembler* masm, Register reg, Arg arg) {
  masm->Move(reg, arg);
  return reg;
}
inline Register ToRegister(MaglevAssembler* masm, Register scratch,
                           Register reg) {
  return reg;
}
inline Register ToRegister(MaglevAssembler* masm, Register scratch,
                           const Input& input) {
  if (input.operand().IsConstant()) {
    input.node()->LoadToRegister(masm, scratch);
    return scratch;
  }
  const compiler::AllocatedOperand& operand =
      compiler::AllocatedOperand::cast(input.operand());
  if (operand.IsRegister()) {
    return ToRegister(input);
  } else {
    DCHECK(operand.IsStackSlot());
    masm->Move(scratch, masm->ToMemOperand(input));
    return scratch;
  }
}

template <typename... Args>
struct PushAllHelper;

template <typename... Args>
inline void PushAll(MaglevAssembler* masm, Args... args) {
  PushAllHelper<Args...>::Push(masm, args...);
}

template <>
struct PushAllHelper<> {
  static void Push(MaglevAssembler* masm) {}
};

template <typename Arg>
struct PushAllHelper<Arg> {
  static void Push(MaglevAssembler* masm, Arg) { FATAL("Unaligned push"); }
};

template <typename Arg1, typename Arg2, typename... Args>
struct PushAllHelper<Arg1, Arg2, Args...> {
  static void Push(MaglevAssembler* masm, Arg1 arg1, Arg2 arg2, Args... args) {
    masm->MacroAssembler::Push(ToRegister(masm, ip0, arg1),
                               ToRegister(masm, ip1, arg2));
    PushAll(masm, args...);
  }
};

template <>
struct PushArgumentsHelper<> {
  static void Push(MaglevAssembler* masm, uint32_t pushed) {
    DCHECK_EQ(pushed % 2, 0);
  }
};

template <typename Arg>
struct PushArgumentsHelper<Arg> {
  static void Push(MaglevAssembler* masm, uint32_t pushed, Arg arg) {
    if constexpr (is_repeat_arguments<Arg>::value) {
      PushRepeat(masm, pushed, arg);
    } else if constexpr (is_push_arguments_iterator<Arg>::value) {
      PushIterator(masm, pushed, arg);
    } else {
      masm->Push(arg, padreg);
    }
  }
};

template <typename Arg1, typename Arg2, typename... Args>
struct PushArgumentsHelper<Arg1, Arg2, Args...> {
  static void Push(MaglevAssembler* masm, uint32_t pushed, Arg1 arg1, Arg2 arg2,
                   Args... args) {
    if constexpr (is_repeat_arguments<Arg1>::value) {
      PushRepeat(masm, pushed, arg1, arg2, args...);
    } else if constexpr (is_push_arguments_iterator<Arg1>::value) {
      PushIterator(masm, pushed, arg1, arg2, args...);
    } else if constexpr (is_repeat_arguments<Arg2>::value ||
                         is_push_arguments_iterator<Arg2>::value) {
      if (arg2.has_more()) {
        masm->Push(arg1, arg2.next());
        PushArguments(masm, pushed + 2, arg2, args...);
      } else {
        PushArguments(masm, pushed, arg1, args...);
      }
    } else {
      masm->Push(arg1, arg2);
      PushArguments(masm, pushed + 2, args...);
    }
  }
};

}  // namespace detail

template <typename... T>
void MaglevAssembler::Push(T... vals) {
  if (sizeof...(vals) % 2 == 0) {
    detail::PushAll(this, vals...);
  } else {
    detail::PushAll(this, padreg, vals...);
  }
}

template <typename... T>
void MaglevAssembler::PushArguments(T... vals) {
  detail::PushArguments(this, 0, vals...);
}

inline MemOperand MaglevAssembler::StackSlotOperand(StackSlot slot) {
  return MemOperand(fp, slot.index);
}

// TODO(Victorgomes): Unify this to use StackSlot struct.
inline MemOperand MaglevAssembler::GetStackSlot(
    const compiler::AllocatedOperand& operand) {
  return MemOperand(fp, GetFramePointerOffsetForStackSlot(operand));
}

inline MemOperand MaglevAssembler::ToMemOperand(
    const compiler::InstructionOperand& operand) {
  return GetStackSlot(compiler::AllocatedOperand::cast(operand));
}

inline MemOperand MaglevAssembler::ToMemOperand(const ValueLocation& location) {
  return ToMemOperand(location.operand());
}

inline void MaglevAssembler::Move(StackSlot dst, Register src) {
  Str(src, StackSlotOperand(dst));
}
inline void MaglevAssembler::Move(StackSlot dst, DoubleRegister src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(Register dst, StackSlot src) {
  Ldr(dst, StackSlotOperand(src));
}
inline void MaglevAssembler::Move(DoubleRegister dst, StackSlot src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(MemOperand dst, Register src) {
  Str(src, dst);
}
inline void MaglevAssembler::Move(MemOperand dst, DoubleRegister src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(Register dst, MemOperand src) {
  Ldr(dst, src);
}
inline void MaglevAssembler::Move(DoubleRegister dst, MemOperand src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(DoubleRegister dst, DoubleRegister src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(Register dst, Smi src) {
  MacroAssembler::Move(dst, src);
}
inline void MaglevAssembler::Move(Register dst, Register src) {
  MacroAssembler::Move(dst, src);
}
inline void MaglevAssembler::Move(Register dst, TaggedIndex i) {
  Mov(dst, i.ptr());
}
inline void MaglevAssembler::Move(Register dst, int32_t i) {
  Mov(dst, Immediate(i));
}
inline void MaglevAssembler::Move(DoubleRegister dst, double n) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(Register dst, Handle<HeapObject> obj) {
  Mov(dst, Operand(obj));
}

inline void MaglevAssembler::Jump(Label* target) { B(target); }

inline void MaglevAssembler::JumpIf(Condition cond, Label* target) {
  b(target, cond);
}

inline void MaglevAssembler::Pop(Register dst) { Pop(padreg, dst); }

inline void MaglevAssembler::AssertStackSizeCorrect() {
  if (v8_flags.debug_code) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.AcquireX();
    Add(scratch, sp,
        RoundUp<2 * kSystemPointerSize>(
            code_gen_state()->stack_slots() * kSystemPointerSize +
            StandardFrameConstants::kFixedFrameSizeFromFp));
    Cmp(scratch, fp);
    Assert(eq, AbortReason::kStackAccessBelowStackPointer);
  }
}

inline void MaglevAssembler::FinishCode() {
  ForceConstantPoolEmissionWithoutJump();
}

inline void MaglevAssembler::MaterialiseValueNode(Register dst,
                                                  ValueNode* value) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}

template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr, Register dst,
                                      Register src) {
  Mov(dst, src);
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr, Register dst,
                                      MemOperand src) {
  switch (repr) {
    case MachineRepresentation::kWord32:
      return Ldr(dst.W(), src);
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTaggedSigned:
      return Ldr(dst, src);
    default:
      UNREACHABLE();
  }
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr,
                                      MemOperand dst, Register src) {
  switch (repr) {
    case MachineRepresentation::kWord32:
      return Str(src.W(), dst);
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTaggedSigned:
      return Str(src, dst);
    default:
      UNREACHABLE();
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_ARM64_MAGLEV_ASSEMBLER_ARM64_INL_H_
