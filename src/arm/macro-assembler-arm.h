// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ARM_MACRO_ASSEMBLER_ARM_H_
#define V8_ARM_MACRO_ASSEMBLER_ARM_H_

#include "src/arm/assembler-arm.h"
#include "src/assembler.h"
#include "src/bailout-reason.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

// Give alias names to registers for calling conventions.
const AsmRegister kReturnRegister0 = {AsmRegister::kCode_r0};
const AsmRegister kReturnRegister1 = {AsmRegister::kCode_r1};
const AsmRegister kReturnRegister2 = {AsmRegister::kCode_r2};
const AsmRegister kJSFunctionRegister = {AsmRegister::kCode_r1};
const AsmRegister kContextRegister = {AsmRegister::kCode_r7};
const AsmRegister kAllocateSizeRegister = {AsmRegister::kCode_r1};
const AsmRegister kInterpreterAccumulatorRegister = {AsmRegister::kCode_r0};
const AsmRegister kInterpreterBytecodeOffsetRegister = {AsmRegister::kCode_r5};
const AsmRegister kInterpreterBytecodeArrayRegister = {AsmRegister::kCode_r6};
const AsmRegister kInterpreterDispatchTableRegister = {AsmRegister::kCode_r8};
const AsmRegister kJavaScriptCallArgCountRegister = {AsmRegister::kCode_r0};
const AsmRegister kJavaScriptCallNewTargetRegister = {AsmRegister::kCode_r3};
const AsmRegister kRuntimeCallFunctionRegister = {AsmRegister::kCode_r1};
const AsmRegister kRuntimeCallArgCountRegister = {AsmRegister::kCode_r0};

// ----------------------------------------------------------------------------
// Static helper functions

// Generate a MemOperand for loading a field from an object.
inline MemOperand FieldMemOperand(AsmRegister object, int offset) {
  return MemOperand(object, offset - kHeapObjectTag);
}


// Give alias names to registers
const AsmRegister cp = {AsmRegister::kCode_r7};  // JavaScript context pointer.
const AsmRegister kRootRegister = {
    AsmRegister::kCode_r10};  // Roots array pointer.

// Flags used for AllocateHeapNumber
enum TaggingMode {
  // Tag the result.
  TAG_RESULT,
  // Don't tag
  DONT_TAG_RESULT
};


enum RememberedSetAction { EMIT_REMEMBERED_SET, OMIT_REMEMBERED_SET };
enum SmiCheck { INLINE_SMI_CHECK, OMIT_SMI_CHECK };
enum PointersToHereCheck {
  kPointersToHereMaybeInteresting,
  kPointersToHereAreAlwaysInteresting
};
enum LinkRegisterStatus { kLRHasNotBeenSaved, kLRHasBeenSaved };

AsmRegister GetRegisterThatIsNotOneOf(AsmRegister reg1,
                                      AsmRegister reg2 = no_reg,
                                      AsmRegister reg3 = no_reg,
                                      AsmRegister reg4 = no_reg,
                                      AsmRegister reg5 = no_reg,
                                      AsmRegister reg6 = no_reg);

#ifdef DEBUG
bool AreAliased(AsmRegister reg1, AsmRegister reg2, AsmRegister reg3 = no_reg,
                AsmRegister reg4 = no_reg, AsmRegister reg5 = no_reg,
                AsmRegister reg6 = no_reg, AsmRegister reg7 = no_reg,
                AsmRegister reg8 = no_reg);
#endif


enum TargetAddressStorageMode {
  CAN_INLINE_TARGET_ADDRESS,
  NEVER_INLINE_TARGET_ADDRESS
};

class TurboAssembler : public Assembler {
 public:
  TurboAssembler(Isolate* isolate, void* buffer, int buffer_size,
                 CodeObjectRequired create_code_object)
      : Assembler(isolate, buffer, buffer_size), isolate_(isolate) {
    if (create_code_object == CodeObjectRequired::kYes) {
      code_object_ =
          Handle<HeapObject>::New(isolate->heap()->undefined_value(), isolate);
    }
  }

  void set_has_frame(bool value) { has_frame_ = value; }
  bool has_frame() const { return has_frame_; }

  Isolate* isolate() const { return isolate_; }

  Handle<HeapObject> CodeObject() {
    DCHECK(!code_object_.is_null());
    return code_object_;
  }

  // Activation support.
  void EnterFrame(StackFrame::Type type,
                  bool load_constant_pool_pointer_reg = false);
  // Returns the pc offset at which the frame ends.
  int LeaveFrame(StackFrame::Type type);

  // Push a fixed frame, consisting of lr, fp
  void PushCommonFrame(AsmRegister marker_reg = no_reg);

  // Generates function and stub prologue code.
  void StubPrologue(StackFrame::Type type);
  void Prologue();

  // Push a standard frame, consisting of lr, fp, context and JS function
  void PushStandardFrame(AsmRegister function_reg);

  void InitializeRootRegister();

  void Push(AsmRegister src) { push(src); }

  void Push(Handle<HeapObject> handle);
  void Push(Smi* smi);

  // Push two registers.  Pushes leftmost register first (to highest address).
  void Push(AsmRegister src1, AsmRegister src2, Condition cond = al) {
    if (src1.code() > src2.code()) {
      stm(db_w, sp, src1.bit() | src2.bit(), cond);
    } else {
      str(src1, MemOperand(sp, 4, NegPreIndex), cond);
      str(src2, MemOperand(sp, 4, NegPreIndex), cond);
    }
  }

  // Push three registers.  Pushes leftmost register first (to highest address).
  void Push(AsmRegister src1, AsmRegister src2, AsmRegister src3,
            Condition cond = al) {
    if (src1.code() > src2.code()) {
      if (src2.code() > src3.code()) {
        stm(db_w, sp, src1.bit() | src2.bit() | src3.bit(), cond);
      } else {
        stm(db_w, sp, src1.bit() | src2.bit(), cond);
        str(src3, MemOperand(sp, 4, NegPreIndex), cond);
      }
    } else {
      str(src1, MemOperand(sp, 4, NegPreIndex), cond);
      Push(src2, src3, cond);
    }
  }

  // Push four registers.  Pushes leftmost register first (to highest address).
  void Push(AsmRegister src1, AsmRegister src2, AsmRegister src3,
            AsmRegister src4, Condition cond = al) {
    if (src1.code() > src2.code()) {
      if (src2.code() > src3.code()) {
        if (src3.code() > src4.code()) {
          stm(db_w, sp, src1.bit() | src2.bit() | src3.bit() | src4.bit(),
              cond);
        } else {
          stm(db_w, sp, src1.bit() | src2.bit() | src3.bit(), cond);
          str(src4, MemOperand(sp, 4, NegPreIndex), cond);
        }
      } else {
        stm(db_w, sp, src1.bit() | src2.bit(), cond);
        Push(src3, src4, cond);
      }
    } else {
      str(src1, MemOperand(sp, 4, NegPreIndex), cond);
      Push(src2, src3, src4, cond);
    }
  }

  // Push five registers.  Pushes leftmost register first (to highest address).
  void Push(AsmRegister src1, AsmRegister src2, AsmRegister src3,
            AsmRegister src4, AsmRegister src5, Condition cond = al) {
    if (src1.code() > src2.code()) {
      if (src2.code() > src3.code()) {
        if (src3.code() > src4.code()) {
          if (src4.code() > src5.code()) {
            stm(db_w, sp,
                src1.bit() | src2.bit() | src3.bit() | src4.bit() | src5.bit(),
                cond);
          } else {
            stm(db_w, sp, src1.bit() | src2.bit() | src3.bit() | src4.bit(),
                cond);
            str(src5, MemOperand(sp, 4, NegPreIndex), cond);
          }
        } else {
          stm(db_w, sp, src1.bit() | src2.bit() | src3.bit(), cond);
          Push(src4, src5, cond);
        }
      } else {
        stm(db_w, sp, src1.bit() | src2.bit(), cond);
        Push(src3, src4, src5, cond);
      }
    } else {
      str(src1, MemOperand(sp, 4, NegPreIndex), cond);
      Push(src2, src3, src4, src5, cond);
    }
  }

  void Pop(AsmRegister dst) { pop(dst); }

  // Pop two registers. Pops rightmost register first (from lower address).
  void Pop(AsmRegister src1, AsmRegister src2, Condition cond = al) {
    DCHECK(!src1.is(src2));
    if (src1.code() > src2.code()) {
      ldm(ia_w, sp, src1.bit() | src2.bit(), cond);
    } else {
      ldr(src2, MemOperand(sp, 4, PostIndex), cond);
      ldr(src1, MemOperand(sp, 4, PostIndex), cond);
    }
  }

  // Pop three registers.  Pops rightmost register first (from lower address).
  void Pop(AsmRegister src1, AsmRegister src2, AsmRegister src3,
           Condition cond = al) {
    DCHECK(!AreAliased(src1, src2, src3));
    if (src1.code() > src2.code()) {
      if (src2.code() > src3.code()) {
        ldm(ia_w, sp, src1.bit() | src2.bit() | src3.bit(), cond);
      } else {
        ldr(src3, MemOperand(sp, 4, PostIndex), cond);
        ldm(ia_w, sp, src1.bit() | src2.bit(), cond);
      }
    } else {
      Pop(src2, src3, cond);
      ldr(src1, MemOperand(sp, 4, PostIndex), cond);
    }
  }

  // Pop four registers.  Pops rightmost register first (from lower address).
  void Pop(AsmRegister src1, AsmRegister src2, AsmRegister src3,
           AsmRegister src4, Condition cond = al) {
    DCHECK(!AreAliased(src1, src2, src3, src4));
    if (src1.code() > src2.code()) {
      if (src2.code() > src3.code()) {
        if (src3.code() > src4.code()) {
          ldm(ia_w, sp, src1.bit() | src2.bit() | src3.bit() | src4.bit(),
              cond);
        } else {
          ldr(src4, MemOperand(sp, 4, PostIndex), cond);
          ldm(ia_w, sp, src1.bit() | src2.bit() | src3.bit(), cond);
        }
      } else {
        Pop(src3, src4, cond);
        ldm(ia_w, sp, src1.bit() | src2.bit(), cond);
      }
    } else {
      Pop(src2, src3, src4, cond);
      ldr(src1, MemOperand(sp, 4, PostIndex), cond);
    }
  }

  // Before calling a C-function from generated code, align arguments on stack.
  // After aligning the frame, non-register arguments must be stored in
  // sp[0], sp[4], etc., not pushed. The argument count assumes all arguments
  // are word sized. If double arguments are used, this function assumes that
  // all double arguments are stored before core registers; otherwise the
  // correct alignment of the double values is not guaranteed.
  // Some compilers/platforms require the stack to be aligned when calling
  // C++ code.
  // Needs a scratch register to do some arithmetic. This register will be
  // trashed.
  void PrepareCallCFunction(int num_reg_arguments,
                            int num_double_registers = 0);

  // Removes current frame and its arguments from the stack preserving
  // the arguments and a return address pushed to the stack for the next call.
  // Both |callee_args_count| and |caller_args_count_reg| do not include
  // receiver. |callee_args_count| is not modified, |caller_args_count_reg|
  // is trashed.
  void PrepareForTailCall(const ParameterCount& callee_args_count,
                          AsmRegister caller_args_count_reg,
                          AsmRegister scratch0, AsmRegister scratch1);

  // There are two ways of passing double arguments on ARM, depending on
  // whether soft or hard floating point ABI is used. These functions
  // abstract parameter passing for the three different ways we call
  // C functions from generated code.
  void MovToFloatParameter(DwVfpRegister src);
  void MovToFloatParameters(DwVfpRegister src1, DwVfpRegister src2);
  void MovToFloatResult(DwVfpRegister src);

  // Calls a C function and cleans up the space for arguments allocated
  // by PrepareCallCFunction. The called function is not allowed to trigger a
  // garbage collection, since that might move the code and invalidate the
  // return address (unless this is somehow accounted for by the called
  // function).
  void CallCFunction(ExternalReference function, int num_arguments);
  void CallCFunction(AsmRegister function, int num_arguments);
  void CallCFunction(ExternalReference function, int num_reg_arguments,
                     int num_double_arguments);
  void CallCFunction(AsmRegister function, int num_reg_arguments,
                     int num_double_arguments);

  void MovFromFloatParameter(DwVfpRegister dst);
  void MovFromFloatResult(DwVfpRegister dst);

  // Calls Abort(msg) if the condition cond is not satisfied.
  // Use --debug_code to enable.
  void Assert(Condition cond, BailoutReason reason);

  // Like Assert(), but always enabled.
  void Check(Condition cond, BailoutReason reason);

  // Print a message to stdout and abort execution.
  void Abort(BailoutReason msg);

  inline bool AllowThisStubCall(CodeStub* stub);

  void LslPair(AsmRegister dst_low, AsmRegister dst_high, AsmRegister src_low,
               AsmRegister src_high, AsmRegister scratch, AsmRegister shift);
  void LslPair(AsmRegister dst_low, AsmRegister dst_high, AsmRegister src_low,
               AsmRegister src_high, uint32_t shift);
  void LsrPair(AsmRegister dst_low, AsmRegister dst_high, AsmRegister src_low,
               AsmRegister src_high, AsmRegister scratch, AsmRegister shift);
  void LsrPair(AsmRegister dst_low, AsmRegister dst_high, AsmRegister src_low,
               AsmRegister src_high, uint32_t shift);
  void AsrPair(AsmRegister dst_low, AsmRegister dst_high, AsmRegister src_low,
               AsmRegister src_high, AsmRegister scratch, AsmRegister shift);
  void AsrPair(AsmRegister dst_low, AsmRegister dst_high, AsmRegister src_low,
               AsmRegister src_high, uint32_t shift);

  // Returns the size of a call in instructions. Note, the value returned is
  // only valid as long as no entries are added to the constant pool between
  // checking the call size and emitting the actual call.
  static int CallSize(AsmRegister target, Condition cond = al);
  int CallSize(Address target, RelocInfo::Mode rmode, Condition cond = al);
  int CallSize(Handle<Code> code,
               RelocInfo::Mode rmode = RelocInfo::CODE_TARGET,
               Condition cond = al);
  int CallStubSize();

  void CallStubDelayed(CodeStub* stub);
  void CallRuntimeDelayed(Zone* zone, Runtime::FunctionId fid,
                          SaveFPRegsMode save_doubles = kDontSaveFPRegs);

  // Jump, Call, and Ret pseudo instructions implementing inter-working.
  void Call(AsmRegister target, Condition cond = al);
  void Call(Address target, RelocInfo::Mode rmode, Condition cond = al,
            TargetAddressStorageMode mode = CAN_INLINE_TARGET_ADDRESS,
            bool check_constant_pool = true);
  void Call(Handle<Code> code, RelocInfo::Mode rmode = RelocInfo::CODE_TARGET,
            Condition cond = al,
            TargetAddressStorageMode mode = CAN_INLINE_TARGET_ADDRESS,
            bool check_constant_pool = true);
  void Call(Label* target);

  // This should only be used when assembling a deoptimizer call because of
  // the CheckConstPool invocation, which is only needed for deoptimization.
  void CallForDeoptimization(Address target, RelocInfo::Mode rmode) {
    Call(target, rmode);
    CheckConstPool(false, false);
  }

  // Emit code to discard a non-negative number of pointer-sized elements
  // from the stack, clobbering only the sp register.
  void Drop(int count, Condition cond = al);
  void Drop(AsmRegister count, Condition cond = al);

  void Ret(Condition cond = al);
  void Ret(int drop, Condition cond = al);

  // Compare single values and move the result to the normal condition flags.
  void VFPCompareAndSetFlags(const SwVfpRegister src1, const SwVfpRegister src2,
                             const Condition cond = al);
  void VFPCompareAndSetFlags(const SwVfpRegister src1, const float src2,
                             const Condition cond = al);

  // Compare double values and move the result to the normal condition flags.
  void VFPCompareAndSetFlags(const DwVfpRegister src1, const DwVfpRegister src2,
                             const Condition cond = al);
  void VFPCompareAndSetFlags(const DwVfpRegister src1, const double src2,
                             const Condition cond = al);

  // If the value is a NaN, canonicalize the value else, do nothing.
  void VFPCanonicalizeNaN(const DwVfpRegister dst, const DwVfpRegister src,
                          const Condition cond = al);
  void VFPCanonicalizeNaN(const DwVfpRegister value,
                          const Condition cond = al) {
    VFPCanonicalizeNaN(value, value, cond);
  }

  void VmovHigh(AsmRegister dst, DwVfpRegister src);
  void VmovHigh(DwVfpRegister dst, AsmRegister src);
  void VmovLow(AsmRegister dst, DwVfpRegister src);
  void VmovLow(DwVfpRegister dst, AsmRegister src);

  void CheckPageFlag(AsmRegister object, AsmRegister scratch, int mask,
                     Condition cc, Label* condition_met);

  // Check whether d16-d31 are available on the CPU. The result is given by the
  // Z condition flag: Z==0 if d16-d31 available, Z==1 otherwise.
  void CheckFor32DRegs(AsmRegister scratch);

  // Does a runtime check for 16/32 FP registers. Either way, pushes 32 double
  // values to location, saving [d0..(d15|d31)].
  void SaveFPRegs(AsmRegister location, AsmRegister scratch);

  // Does a runtime check for 16/32 FP registers. Either way, pops 32 double
  // values to location, restoring [d0..(d15|d31)].
  void RestoreFPRegs(AsmRegister location, AsmRegister scratch);

  void PushCallerSaved(SaveFPRegsMode fp_mode, AsmRegister exclusion1 = no_reg,
                       AsmRegister exclusion2 = no_reg,
                       AsmRegister exclusion3 = no_reg);
  void PopCallerSaved(SaveFPRegsMode fp_mode, AsmRegister exclusion1 = no_reg,
                      AsmRegister exclusion2 = no_reg,
                      AsmRegister exclusion3 = no_reg);
  void Jump(AsmRegister target, Condition cond = al);
  void Jump(Address target, RelocInfo::Mode rmode, Condition cond = al);
  void Jump(Handle<Code> code, RelocInfo::Mode rmode, Condition cond = al);

  // Perform a floating-point min or max operation with the
  // (IEEE-754-compatible) semantics of ARM64's fmin/fmax. Some cases, typically
  // NaNs or +/-0.0, are expected to be rare and are handled in out-of-line
  // code. The specific behaviour depends on supported instructions.
  //
  // These functions assume (and assert) that !left.is(right). It is permitted
  // for the result to alias either input register.
  void FloatMax(SwVfpRegister result, SwVfpRegister left, SwVfpRegister right,
                Label* out_of_line);
  void FloatMin(SwVfpRegister result, SwVfpRegister left, SwVfpRegister right,
                Label* out_of_line);
  void FloatMax(DwVfpRegister result, DwVfpRegister left, DwVfpRegister right,
                Label* out_of_line);
  void FloatMin(DwVfpRegister result, DwVfpRegister left, DwVfpRegister right,
                Label* out_of_line);

  // Generate out-of-line cases for the macros above.
  void FloatMaxOutOfLine(SwVfpRegister result, SwVfpRegister left,
                         SwVfpRegister right);
  void FloatMinOutOfLine(SwVfpRegister result, SwVfpRegister left,
                         SwVfpRegister right);
  void FloatMaxOutOfLine(DwVfpRegister result, DwVfpRegister left,
                         DwVfpRegister right);
  void FloatMinOutOfLine(DwVfpRegister result, DwVfpRegister left,
                         DwVfpRegister right);

  void ExtractLane(AsmRegister dst, QwNeonRegister src, NeonDataType dt,
                   int lane);
  void ExtractLane(AsmRegister dst, DwVfpRegister src, NeonDataType dt,
                   int lane);
  void ExtractLane(SwVfpRegister dst, QwNeonRegister src, int lane);
  void ReplaceLane(QwNeonRegister dst, QwNeonRegister src, AsmRegister src_lane,
                   NeonDataType dt, int lane);
  void ReplaceLane(QwNeonRegister dst, QwNeonRegister src,
                   SwVfpRegister src_lane, int lane);

  // AsmRegister move. May do nothing if the registers are identical.
  void Move(AsmRegister dst, Smi* smi);
  void Move(AsmRegister dst, Handle<HeapObject> value);
  void Move(AsmRegister dst, AsmRegister src, Condition cond = al);
  void Move(AsmRegister dst, const Operand& src, SBit sbit = LeaveCC,
            Condition cond = al) {
    if (!src.IsRegister() || !src.rm().is(dst) || sbit != LeaveCC) {
      mov(dst, src, sbit, cond);
    }
  }
  void Move(SwVfpRegister dst, SwVfpRegister src, Condition cond = al);
  void Move(DwVfpRegister dst, DwVfpRegister src, Condition cond = al);
  void Move(QwNeonRegister dst, QwNeonRegister src);

  // Simulate s-register moves for imaginary s32 - s63 registers.
  void VmovExtended(AsmRegister dst, int src_code);
  void VmovExtended(int dst_code, AsmRegister src);
  // Move between s-registers and imaginary s-registers.
  void VmovExtended(int dst_code, int src_code);
  void VmovExtended(int dst_code, const MemOperand& src);
  void VmovExtended(const MemOperand& dst, int src_code);

  // AsmRegister swap.
  void Swap(DwVfpRegister srcdst0, DwVfpRegister srcdst1);
  void Swap(QwNeonRegister srcdst0, QwNeonRegister srcdst1);

  // Get the actual activation frame alignment for target environment.
  static int ActivationFrameAlignment();

  void Bfc(AsmRegister dst, AsmRegister src, int lsb, int width,
           Condition cond = al);

  void SmiUntag(AsmRegister reg, SBit s = LeaveCC) {
    mov(reg, Operand::SmiUntag(reg), s);
  }
  void SmiUntag(AsmRegister dst, AsmRegister src, SBit s = LeaveCC) {
    mov(dst, Operand::SmiUntag(src), s);
  }

  // Load an object from the root table.
  void LoadRoot(AsmRegister destination, Heap::RootListIndex index,
                Condition cond = al);

  // Jump if the register contains a smi.
  void JumpIfSmi(AsmRegister value, Label* smi_label);

  // Performs a truncating conversion of a floating point number as used by
  // the JS bitwise operations. See ECMA-262 9.5: ToInt32. Goes to 'done' if it
  // succeeds, otherwise falls through if result is saturated. On return
  // 'result' either holds answer, or is clobbered on fall through.
  //
  // Only public for the test code in test-code-stubs-arm.cc.
  void TryInlineTruncateDoubleToI(AsmRegister result, DwVfpRegister input,
                                  Label* done);

  // Performs a truncating conversion of a floating point number as used by
  // the JS bitwise operations. See ECMA-262 9.5: ToInt32.
  // Exits with 'result' holding the answer.
  void TruncateDoubleToIDelayed(Zone* zone, AsmRegister result,
                                DwVfpRegister double_input);

  // EABI variant for double arguments in use.
  bool use_eabi_hardfloat() {
#ifdef __arm__
    return base::OS::ArmUsingHardFloat();
#elif USE_EABI_HARDFLOAT
    return true;
#else
    return false;
#endif
  }

 private:
  bool has_frame_ = false;
  Isolate* const isolate_;
  // This handle will be patched with the code object on installation.
  Handle<HeapObject> code_object_;

  // Compare single values and then load the fpscr flags to a register.
  void VFPCompareAndLoadFlags(const SwVfpRegister src1,
                              const SwVfpRegister src2,
                              const AsmRegister fpscr_flags,
                              const Condition cond = al);
  void VFPCompareAndLoadFlags(const SwVfpRegister src1, const float src2,
                              const AsmRegister fpscr_flags,
                              const Condition cond = al);

  // Compare double values and then load the fpscr flags to a register.
  void VFPCompareAndLoadFlags(const DwVfpRegister src1,
                              const DwVfpRegister src2,
                              const AsmRegister fpscr_flags,
                              const Condition cond = al);
  void VFPCompareAndLoadFlags(const DwVfpRegister src1, const double src2,
                              const AsmRegister fpscr_flags,
                              const Condition cond = al);

  void Jump(intptr_t target, RelocInfo::Mode rmode, Condition cond = al);

  // Implementation helpers for FloatMin and FloatMax.
  template <typename T>
  void FloatMaxHelper(T result, T left, T right, Label* out_of_line);
  template <typename T>
  void FloatMinHelper(T result, T left, T right, Label* out_of_line);
  template <typename T>
  void FloatMaxOutOfLineHelper(T result, T left, T right);
  template <typename T>
  void FloatMinOutOfLineHelper(T result, T left, T right);

  int CalculateStackPassedWords(int num_reg_arguments,
                                int num_double_arguments);

  void CallCFunctionHelper(AsmRegister function, int num_reg_arguments,
                           int num_double_arguments);
};

// MacroAssembler implements a collection of frequently used macros.
class MacroAssembler : public TurboAssembler {
 public:
  MacroAssembler(Isolate* isolate, void* buffer, int size,
                 CodeObjectRequired create_code_object);

  // Used for patching in calls to the deoptimizer.
  void CallDeoptimizer(Address target);
  static int CallDeoptimizerSize();

  // Emit code that loads |parameter_index|'th parameter from the stack to
  // the register according to the CallInterfaceDescriptor definition.
  // |sp_to_caller_sp_offset_in_words| specifies the number of words pushed
  // below the caller's sp.
  template <class Descriptor>
  void LoadParameterFromStack(
      AsmRegister reg, typename Descriptor::ParameterIndices parameter_index,
      int sp_to_ra_offset_in_words = 0) {
    DCHECK(Descriptor::kPassLastArgsOnStack);
    UNIMPLEMENTED();
  }

  // Swap two registers.  If the scratch register is omitted then a slightly
  // less efficient form using xor instead of mov is emitted.
  void Swap(AsmRegister reg1, AsmRegister reg2, AsmRegister scratch = no_reg,
            Condition cond = al);

  void Mls(AsmRegister dst, AsmRegister src1, AsmRegister src2,
           AsmRegister srcA, Condition cond = al);
  void And(AsmRegister dst, AsmRegister src1, const Operand& src2,
           Condition cond = al);
  void Ubfx(AsmRegister dst, AsmRegister src, int lsb, int width,
            Condition cond = al);
  void Sbfx(AsmRegister dst, AsmRegister src, int lsb, int width,
            Condition cond = al);

  void Load(AsmRegister dst, const MemOperand& src, Representation r);
  void Store(AsmRegister src, const MemOperand& dst, Representation r);

  // ---------------------------------------------------------------------------
  // GC Support

  enum RememberedSetFinalAction { kReturnAtEnd, kFallThroughAtEnd };

  // Record in the remembered set the fact that we have a pointer to new space
  // at the address pointed to by the addr register.  Only works if addr is not
  // in new space.
  void RememberedSetHelper(AsmRegister object,  // Used for debug code.
                           AsmRegister addr, AsmRegister scratch,
                           SaveFPRegsMode save_fp,
                           RememberedSetFinalAction and_then);

  // Check if object is in new space.  Jumps if the object is not in new space.
  // The register scratch can be object itself, but scratch will be clobbered.
  void JumpIfNotInNewSpace(AsmRegister object, AsmRegister scratch,
                           Label* branch) {
    InNewSpace(object, scratch, eq, branch);
  }

  // Check if object is in new space.  Jumps if the object is in new space.
  // The register scratch can be object itself, but it will be clobbered.
  void JumpIfInNewSpace(AsmRegister object, AsmRegister scratch,
                        Label* branch) {
    InNewSpace(object, scratch, ne, branch);
  }

  // Check if an object has a given incremental marking color.
  void HasColor(AsmRegister object, AsmRegister scratch0, AsmRegister scratch1,
                Label* has_color, int first_bit, int second_bit);

  void JumpIfBlack(AsmRegister object, AsmRegister scratch0,
                   AsmRegister scratch1, Label* on_black);

  // Checks the color of an object.  If the object is white we jump to the
  // incremental marker.
  void JumpIfWhite(AsmRegister value, AsmRegister scratch1,
                   AsmRegister scratch2, AsmRegister scratch3,
                   Label* value_is_white);

  // Notify the garbage collector that we wrote a pointer into an object.
  // |object| is the object being stored into, |value| is the object being
  // stored.  value and scratch registers are clobbered by the operation.
  // The offset is the offset from the start of the object, not the offset from
  // the tagged HeapObject pointer.  For use with FieldMemOperand(reg, off).
  void RecordWriteField(
      AsmRegister object, int offset, AsmRegister value, AsmRegister scratch,
      LinkRegisterStatus lr_status, SaveFPRegsMode save_fp,
      RememberedSetAction remembered_set_action = EMIT_REMEMBERED_SET,
      SmiCheck smi_check = INLINE_SMI_CHECK,
      PointersToHereCheck pointers_to_here_check_for_value =
          kPointersToHereMaybeInteresting);

  // As above, but the offset has the tag presubtracted.  For use with
  // MemOperand(reg, off).
  inline void RecordWriteContextSlot(
      AsmRegister context, int offset, AsmRegister value, AsmRegister scratch,
      LinkRegisterStatus lr_status, SaveFPRegsMode save_fp,
      RememberedSetAction remembered_set_action = EMIT_REMEMBERED_SET,
      SmiCheck smi_check = INLINE_SMI_CHECK,
      PointersToHereCheck pointers_to_here_check_for_value =
          kPointersToHereMaybeInteresting) {
    RecordWriteField(context, offset + kHeapObjectTag, value, scratch,
                     lr_status, save_fp, remembered_set_action, smi_check,
                     pointers_to_here_check_for_value);
  }

  void RecordWriteForMap(AsmRegister object, AsmRegister map, AsmRegister dst,
                         LinkRegisterStatus lr_status, SaveFPRegsMode save_fp);

  // For a given |object| notify the garbage collector that the slot |address|
  // has been written.  |value| is the object being stored. The value and
  // address registers are clobbered by the operation.
  void RecordWrite(
      AsmRegister object, AsmRegister address, AsmRegister value,
      LinkRegisterStatus lr_status, SaveFPRegsMode save_fp,
      RememberedSetAction remembered_set_action = EMIT_REMEMBERED_SET,
      SmiCheck smi_check = INLINE_SMI_CHECK,
      PointersToHereCheck pointers_to_here_check_for_value =
          kPointersToHereMaybeInteresting);

  // Push and pop the registers that can hold pointers, as defined by the
  // RegList constant kSafepointSavedRegisters.
  void PushSafepointRegisters();
  void PopSafepointRegisters();

  // Enter exit frame.
  // stack_space - extra stack space, used for alignment before call to C.
  void EnterExitFrame(bool save_doubles, int stack_space = 0,
                      StackFrame::Type frame_type = StackFrame::EXIT);

  // Leave the current exit frame. Expects the return value in r0.
  // Expect the number of values, pushed prior to the exit frame, to
  // remove in a register (or no_reg, if there is nothing to remove).
  void LeaveExitFrame(bool save_doubles, AsmRegister argument_count,
                      bool restore_context,
                      bool argument_count_is_length = false);

  // Load the global object from the current context.
  void LoadGlobalObject(AsmRegister dst) {
    LoadNativeContextSlot(Context::EXTENSION_INDEX, dst);
  }

  // Load the global proxy from the current context.
  void LoadGlobalProxy(AsmRegister dst) {
    LoadNativeContextSlot(Context::GLOBAL_PROXY_INDEX, dst);
  }

  void LoadNativeContextSlot(int index, AsmRegister dst);

  // Load the initial map from the global function. The registers
  // function and map can be the same, function is then overwritten.
  void LoadGlobalFunctionInitialMap(AsmRegister function, AsmRegister map,
                                    AsmRegister scratch);

  // ---------------------------------------------------------------------------
  // JavaScript invokes

  // Invoke the JavaScript function code by either calling or jumping.
  void InvokeFunctionCode(AsmRegister function, AsmRegister new_target,
                          const ParameterCount& expected,
                          const ParameterCount& actual, InvokeFlag flag);

  // On function call, call into the debugger if necessary.
  void CheckDebugHook(AsmRegister fun, AsmRegister new_target,
                      const ParameterCount& expected,
                      const ParameterCount& actual);

  // Invoke the JavaScript function in the given register. Changes the
  // current context to the context in the function before invoking.
  void InvokeFunction(AsmRegister function, AsmRegister new_target,
                      const ParameterCount& actual, InvokeFlag flag);

  void InvokeFunction(AsmRegister function, const ParameterCount& expected,
                      const ParameterCount& actual, InvokeFlag flag);

  void InvokeFunction(Handle<JSFunction> function,
                      const ParameterCount& expected,
                      const ParameterCount& actual, InvokeFlag flag);

  // Frame restart support
  void MaybeDropFrames();

  // Exception handling

  // Push a new stack handler and link into stack handler chain.
  void PushStackHandler();

  // Unlink the stack handler on top of the stack from the stack handler chain.
  // Must preserve the result register.
  void PopStackHandler();

  // ---------------------------------------------------------------------------
  // Allocation support

  // Allocate an object in new space or old space. The object_size is
  // specified either in bytes or in words if the allocation flag SIZE_IN_WORDS
  // is passed. If the space is exhausted control continues at the gc_required
  // label. The allocated object is returned in result. If the flag
  // tag_allocated_object is true the result is tagged as as a heap object.
  // All registers are clobbered also when control continues at the gc_required
  // label.
  void Allocate(int object_size, AsmRegister result, AsmRegister scratch1,
                AsmRegister scratch2, Label* gc_required,
                AllocationFlags flags);

  // Allocate and initialize a JSValue wrapper with the specified {constructor}
  // and {value}.
  void AllocateJSValue(AsmRegister result, AsmRegister constructor,
                       AsmRegister value, AsmRegister scratch1,
                       AsmRegister scratch2, Label* gc_required);

  // ---------------------------------------------------------------------------
  // Support functions.

  // Machine code version of Map::GetConstructor().
  // |temp| holds |result|'s map when done, and |temp2| its instance type.
  void GetMapConstructor(AsmRegister result, AsmRegister map, AsmRegister temp,
                         AsmRegister temp2);

  // Compare object type for heap object.  heap_object contains a non-Smi
  // whose object type should be compared with the given type.  This both
  // sets the flags and leaves the object type in the type_reg register.
  // It leaves the map in the map register (unless the type_reg and map register
  // are the same register).  It leaves the heap object in the heap_object
  // register unless the heap_object register is the same register as one of the
  // other registers.
  // Type_reg can be no_reg. In that case a scratch register is used.
  void CompareObjectType(AsmRegister heap_object, AsmRegister map,
                         AsmRegister type_reg, InstanceType type);

  // Compare instance type in a map.  map contains a valid map object whose
  // object type should be compared with the given type.  This both
  // sets the flags and leaves the object type in the type_reg register.
  void CompareInstanceType(AsmRegister map, AsmRegister type_reg,
                           InstanceType type);

  // Compare an object's map with the specified map and its transitioned
  // elements maps if mode is ALLOW_ELEMENT_TRANSITION_MAPS. Condition flags are
  // set with result of map compare. If multiple map compares are required, the
  // compare sequences branches to early_success.
  void CompareMap(AsmRegister obj, AsmRegister scratch, Handle<Map> map,
                  Label* early_success);

  // As above, but the map of the object is already loaded into the register
  // which is preserved by the code generated.
  void CompareMap(AsmRegister obj_map, Handle<Map> map, Label* early_success);

  // Check if the map of an object is equal to a specified map and branch to
  // label if not. Skip the smi check if not required (object is known to be a
  // heap object). If mode is ALLOW_ELEMENT_TRANSITION_MAPS, then also match
  // against maps that are ElementsKind transition maps of the specified map.
  void CheckMap(AsmRegister obj, AsmRegister scratch, Handle<Map> map,
                Label* fail, SmiCheckType smi_check_type);

  void CheckMap(AsmRegister obj, AsmRegister scratch, Heap::RootListIndex index,
                Label* fail, SmiCheckType smi_check_type);

  void GetWeakValue(AsmRegister value, Handle<WeakCell> cell);

  // Load the value of the weak cell in the value register. Branch to the given
  // miss label if the weak cell was cleared.
  void LoadWeakValue(AsmRegister value, Handle<WeakCell> cell, Label* miss);

  // Compare the object in a register to a value from the root list.
  // Acquires a scratch register.
  void CompareRoot(AsmRegister obj, Heap::RootListIndex index);
  void PushRoot(Heap::RootListIndex index) {
    UseScratchRegisterScope temps(this);
    AsmRegister scratch = temps.Acquire();
    LoadRoot(scratch, index);
    Push(scratch);
  }

  // Compare the object in a register to a value and jump if they are equal.
  void JumpIfRoot(AsmRegister with, Heap::RootListIndex index,
                  Label* if_equal) {
    CompareRoot(with, index);
    b(eq, if_equal);
  }

  // Compare the object in a register to a value and jump if they are not equal.
  void JumpIfNotRoot(AsmRegister with, Heap::RootListIndex index,
                     Label* if_not_equal) {
    CompareRoot(with, index);
    b(ne, if_not_equal);
  }

  // Load the value of a smi object into a double register.
  // The register value must be between d0 and d15.
  void SmiToDouble(LowDwVfpRegister value, AsmRegister smi);

  // Try to convert a double to a signed 32-bit integer.
  // Z flag set to one and result assigned if the conversion is exact.
  void TryDoubleToInt32Exact(AsmRegister result, DwVfpRegister double_input,
                             LowDwVfpRegister double_scratch);

  // ---------------------------------------------------------------------------
  // Runtime calls

  // Call a code stub.
  void CallStub(CodeStub* stub,
                Condition cond = al);

  // Call a code stub.
  void TailCallStub(CodeStub* stub, Condition cond = al);

  // Call a runtime routine.
  void CallRuntime(const Runtime::Function* f,
                   int num_arguments,
                   SaveFPRegsMode save_doubles = kDontSaveFPRegs);

  // Convenience function: Same as above, but takes the fid instead.
  void CallRuntime(Runtime::FunctionId fid,
                   SaveFPRegsMode save_doubles = kDontSaveFPRegs) {
    const Runtime::Function* function = Runtime::FunctionForId(fid);
    CallRuntime(function, function->nargs, save_doubles);
  }

  // Convenience function: Same as above, but takes the fid instead.
  void CallRuntime(Runtime::FunctionId fid, int num_arguments,
                   SaveFPRegsMode save_doubles = kDontSaveFPRegs) {
    CallRuntime(Runtime::FunctionForId(fid), num_arguments, save_doubles);
  }

  // Convenience function: tail call a runtime routine (jump).
  void TailCallRuntime(Runtime::FunctionId fid);

  // Jump to a runtime routine.
  void JumpToExternalReference(const ExternalReference& builtin,
                               bool builtin_exit_frame = false);

  // ---------------------------------------------------------------------------
  // StatsCounter support

  void IncrementCounter(StatsCounter* counter, int value, AsmRegister scratch1,
                        AsmRegister scratch2);
  void DecrementCounter(StatsCounter* counter, int value, AsmRegister scratch1,
                        AsmRegister scratch2);

  // ---------------------------------------------------------------------------
  // Smi utilities

  void SmiTag(AsmRegister reg, SBit s = LeaveCC);
  void SmiTag(AsmRegister dst, AsmRegister src, SBit s = LeaveCC);

  // Untag the source value into destination and jump if source is a smi.
  // Souce and destination can be the same register.
  void UntagAndJumpIfSmi(AsmRegister dst, AsmRegister src, Label* smi_case);

  // Test if the register contains a smi (Z == 0 (eq) if true).
  void SmiTst(AsmRegister value);
  // Jump if either of the registers contain a non-smi.
  void JumpIfNotSmi(AsmRegister value, Label* not_smi_label);
  // Jump if either of the registers contain a smi.
  void JumpIfEitherSmi(AsmRegister reg1, AsmRegister reg2,
                       Label* on_either_smi);

  // Abort execution if argument is a smi, enabled via --debug-code.
  void AssertNotSmi(AsmRegister object);
  void AssertSmi(AsmRegister object);

  // Abort execution if argument is not a FixedArray, enabled via --debug-code.
  void AssertFixedArray(AsmRegister object);

  // Abort execution if argument is not a JSFunction, enabled via --debug-code.
  void AssertFunction(AsmRegister object);

  // Abort execution if argument is not a JSBoundFunction,
  // enabled via --debug-code.
  void AssertBoundFunction(AsmRegister object);

  // Abort execution if argument is not a JSGeneratorObject (or subclass),
  // enabled via --debug-code.
  void AssertGeneratorObject(AsmRegister object);

  // Abort execution if argument is not undefined or an AllocationSite, enabled
  // via --debug-code.
  void AssertUndefinedOrAllocationSite(AsmRegister object, AsmRegister scratch);

  // ---------------------------------------------------------------------------
  // String utilities

  // Checks if both objects are sequential one-byte strings and jumps to label
  // if either is not. Assumes that neither object is a smi.
  void JumpIfNonSmisNotBothSequentialOneByteStrings(AsmRegister object1,
                                                    AsmRegister object2,
                                                    AsmRegister scratch1,
                                                    AsmRegister scratch2,
                                                    Label* failure);

  // Checks if both instance types are sequential one-byte strings and jumps to
  // label if either is not.
  void JumpIfBothInstanceTypesAreNotSequentialOneByte(
      AsmRegister first_object_instance_type,
      AsmRegister second_object_instance_type, AsmRegister scratch1,
      AsmRegister scratch2, Label* failure);

  void JumpIfNotUniqueNameInstanceType(AsmRegister reg, Label* not_unique_name);

  void LoadInstanceDescriptors(AsmRegister map, AsmRegister descriptors);
  void LoadAccessor(AsmRegister dst, AsmRegister holder, int accessor_index,
                    AccessorComponent accessor);

  template <typename Field>
  void DecodeField(AsmRegister dst, AsmRegister src) {
    Ubfx(dst, src, Field::kShift, Field::kSize);
  }

  template <typename Field>
  void DecodeField(AsmRegister reg) {
    DecodeField<Field>(reg, reg);
  }

  void EnterBuiltinFrame(AsmRegister context, AsmRegister target,
                         AsmRegister argc);
  void LeaveBuiltinFrame(AsmRegister context, AsmRegister target,
                         AsmRegister argc);

 private:
  // Helper functions for generating invokes.
  void InvokePrologue(const ParameterCount& expected,
                      const ParameterCount& actual, Label* done,
                      bool* definitely_mismatches, InvokeFlag flag);

  // Helper for implementing JumpIfNotInNewSpace and JumpIfInNewSpace.
  void InNewSpace(AsmRegister object, AsmRegister scratch,
                  Condition cond,  // eq for new space, ne otherwise.
                  Label* branch);

  // Helper for finding the mark bits for an address.  Afterwards, the
  // bitmap register points at the word with the mark bits and the mask
  // the position of the first bit.  Leaves addr_reg unchanged.
  inline void GetMarkBits(AsmRegister addr_reg, AsmRegister bitmap_reg,
                          AsmRegister mask_reg);

  // Compute memory operands for safepoint stack slots.
  static int SafepointRegisterStackIndex(int reg_code);

  // Needs access to SafepointRegisterStackIndex for compiled frame
  // traversal.
  friend class StandardFrame;
};

// The code patcher is used to patch (typically) small parts of code e.g. for
// debugging and other types of instrumentation. When using the code patcher
// the exact number of bytes specified must be emitted. It is not legal to emit
// relocation information. If any of these constraints are violated it causes
// an assertion to fail.
class CodePatcher {
 public:
  enum FlushICache {
    FLUSH,
    DONT_FLUSH
  };

  CodePatcher(Isolate* isolate, byte* address, int instructions,
              FlushICache flush_cache = FLUSH);
  ~CodePatcher();

  // Macro assembler to emit code.
  MacroAssembler* masm() { return &masm_; }

  // Emit an instruction directly.
  void Emit(Instr instr);

  // Emit an address directly.
  void Emit(Address addr);

  // Emit the condition part of an instruction leaving the rest of the current
  // instruction unchanged.
  void EmitCondition(Condition cond);

 private:
  byte* address_;  // The address of the code being patched.
  int size_;  // Number of bytes of the expected patch size.
  MacroAssembler masm_;  // Macro assembler used to generate the code.
  FlushICache flush_cache_;  // Whether to flush the I cache after patching.
};


// -----------------------------------------------------------------------------
// Static helper functions.

inline MemOperand ContextMemOperand(AsmRegister context, int index = 0) {
  return MemOperand(context, Context::SlotOffset(index));
}


inline MemOperand NativeContextMemOperand() {
  return ContextMemOperand(cp, Context::NATIVE_CONTEXT_INDEX);
}

#define ACCESS_MASM(masm) masm->

}  // namespace internal
}  // namespace v8

#endif  // V8_ARM_MACRO_ASSEMBLER_ARM_H_
