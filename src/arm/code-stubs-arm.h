// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ARM_CODE_STUBS_ARM_H_
#define V8_ARM_CODE_STUBS_ARM_H_

namespace v8 {
namespace internal {


class StringHelper : public AllStatic {
 public:
  // Compares two flat one-byte strings and returns result in r0.
  static void GenerateCompareFlatOneByteStrings(
      MacroAssembler* masm, AsmRegister left, AsmRegister right,
      AsmRegister scratch1, AsmRegister scratch2, AsmRegister scratch3,
      AsmRegister scratch4);

  // Compares two flat one-byte strings for equality and returns result in r0.
  static void GenerateFlatOneByteStringEquals(
      MacroAssembler* masm, AsmRegister left, AsmRegister right,
      AsmRegister scratch1, AsmRegister scratch2, AsmRegister scratch3);

 private:
  static void GenerateOneByteCharsCompareLoop(
      MacroAssembler* masm, AsmRegister left, AsmRegister right,
      AsmRegister length, AsmRegister scratch1, AsmRegister scratch2,
      Label* chars_not_equal);

  DISALLOW_IMPLICIT_CONSTRUCTORS(StringHelper);
};


class RecordWriteStub: public PlatformCodeStub {
 public:
  RecordWriteStub(Isolate* isolate, AsmRegister object, AsmRegister value,
                  AsmRegister address,
                  RememberedSetAction remembered_set_action,
                  SaveFPRegsMode fp_mode)
      : PlatformCodeStub(isolate),
        regs_(object,   // An input reg.
              address,  // An input reg.
              value) {  // One scratch reg.
    minor_key_ = ObjectBits::encode(object.code()) |
                 ValueBits::encode(value.code()) |
                 AddressBits::encode(address.code()) |
                 RememberedSetActionBits::encode(remembered_set_action) |
                 SaveFPRegsModeBits::encode(fp_mode);
  }

  RecordWriteStub(uint32_t key, Isolate* isolate)
      : PlatformCodeStub(key, isolate), regs_(object(), address(), value()) {}

  enum Mode {
    STORE_BUFFER_ONLY,
    INCREMENTAL,
    INCREMENTAL_COMPACTION
  };

  bool SometimesSetsUpAFrame() override { return false; }

  static void PatchBranchIntoNop(MacroAssembler* masm, int pos) {
    masm->instr_at_put(pos, (masm->instr_at(pos) & ~B27) | (B24 | B20));
    DCHECK(Assembler::IsTstImmediate(masm->instr_at(pos)));
  }

  static void PatchNopIntoBranch(MacroAssembler* masm, int pos) {
    masm->instr_at_put(pos, (masm->instr_at(pos) & ~(B24 | B20)) | B27);
    DCHECK(Assembler::IsBranch(masm->instr_at(pos)));
  }

  static Mode GetMode(Code* stub) {
    Instr first_instruction = Assembler::instr_at(stub->instruction_start());
    Instr second_instruction = Assembler::instr_at(stub->instruction_start() +
                                                   Assembler::kInstrSize);

    if (Assembler::IsBranch(first_instruction)) {
      return INCREMENTAL;
    }

    DCHECK(Assembler::IsTstImmediate(first_instruction));

    if (Assembler::IsBranch(second_instruction)) {
      return INCREMENTAL_COMPACTION;
    }

    DCHECK(Assembler::IsTstImmediate(second_instruction));

    return STORE_BUFFER_ONLY;
  }

  static void Patch(Code* stub, Mode mode) {
    MacroAssembler masm(stub->GetIsolate(), stub->instruction_start(),
                        stub->instruction_size(), CodeObjectRequired::kNo);
    switch (mode) {
      case STORE_BUFFER_ONLY:
        DCHECK(GetMode(stub) == INCREMENTAL ||
               GetMode(stub) == INCREMENTAL_COMPACTION);
        PatchBranchIntoNop(&masm, 0);
        PatchBranchIntoNop(&masm, Assembler::kInstrSize);
        break;
      case INCREMENTAL:
        DCHECK(GetMode(stub) == STORE_BUFFER_ONLY);
        PatchNopIntoBranch(&masm, 0);
        break;
      case INCREMENTAL_COMPACTION:
        DCHECK(GetMode(stub) == STORE_BUFFER_ONLY);
        PatchNopIntoBranch(&masm, Assembler::kInstrSize);
        break;
    }
    DCHECK(GetMode(stub) == mode);
    Assembler::FlushICache(stub->GetIsolate(), stub->instruction_start(),
                           2 * Assembler::kInstrSize);
  }

  DEFINE_NULL_CALL_INTERFACE_DESCRIPTOR();

 private:
  // This is a helper class for freeing up 3 scratch registers.  The input is
  // two registers that must be preserved and one scratch register provided by
  // the caller.
  class RegisterAllocation {
   public:
    RegisterAllocation(AsmRegister object, AsmRegister address,
                       AsmRegister scratch0)
        : object_(object), address_(address), scratch0_(scratch0) {
      DCHECK(!AreAliased(scratch0, object, address, no_reg));
      scratch1_ = GetRegisterThatIsNotOneOf(object_, address_, scratch0_);
    }

    void Save(MacroAssembler* masm) {
      DCHECK(!AreAliased(object_, address_, scratch1_, scratch0_));
      // We don't have to save scratch0_ because it was given to us as
      // a scratch register.
      masm->push(scratch1_);
    }

    void Restore(MacroAssembler* masm) {
      masm->pop(scratch1_);
    }

    // If we have to call into C then we need to save and restore all caller-
    // saved registers that were not already preserved.  The scratch registers
    // will be restored by other means so we don't bother pushing them here.
    void SaveCallerSaveRegisters(MacroAssembler* masm, SaveFPRegsMode mode) {
      masm->stm(db_w, sp, (kCallerSaved | lr.bit()) & ~scratch1_.bit());
      if (mode == kSaveFPRegs) {
        masm->SaveFPRegs(sp, scratch0_);
      }
    }

    inline void RestoreCallerSaveRegisters(MacroAssembler*masm,
                                           SaveFPRegsMode mode) {
      if (mode == kSaveFPRegs) {
        masm->RestoreFPRegs(sp, scratch0_);
      }
      masm->ldm(ia_w, sp, (kCallerSaved | lr.bit()) & ~scratch1_.bit());
    }

    inline AsmRegister object() { return object_; }
    inline AsmRegister address() { return address_; }
    inline AsmRegister scratch0() { return scratch0_; }
    inline AsmRegister scratch1() { return scratch1_; }

   private:
    AsmRegister object_;
    AsmRegister address_;
    AsmRegister scratch0_;
    AsmRegister scratch1_;

    friend class RecordWriteStub;
  };

  enum OnNoNeedToInformIncrementalMarker {
    kReturnOnNoNeedToInformIncrementalMarker,
    kUpdateRememberedSetOnNoNeedToInformIncrementalMarker
  };

  inline Major MajorKey() const final { return RecordWrite; }

  void Generate(MacroAssembler* masm) override;
  void GenerateIncremental(MacroAssembler* masm, Mode mode);
  void CheckNeedsToInformIncrementalMarker(
      MacroAssembler* masm,
      OnNoNeedToInformIncrementalMarker on_no_need,
      Mode mode);
  void InformIncrementalMarker(MacroAssembler* masm);

  void Activate(Code* code) override;

  AsmRegister object() const {
    return AsmRegister::from_code(ObjectBits::decode(minor_key_));
  }

  AsmRegister value() const {
    return AsmRegister::from_code(ValueBits::decode(minor_key_));
  }

  AsmRegister address() const {
    return AsmRegister::from_code(AddressBits::decode(minor_key_));
  }

  RememberedSetAction remembered_set_action() const {
    return RememberedSetActionBits::decode(minor_key_);
  }

  SaveFPRegsMode save_fp_regs_mode() const {
    return SaveFPRegsModeBits::decode(minor_key_);
  }

  class ObjectBits: public BitField<int, 0, 4> {};
  class ValueBits: public BitField<int, 4, 4> {};
  class AddressBits: public BitField<int, 8, 4> {};
  class RememberedSetActionBits: public BitField<RememberedSetAction, 12, 1> {};
  class SaveFPRegsModeBits: public BitField<SaveFPRegsMode, 13, 1> {};

  Label slow_;
  RegisterAllocation regs_;

  DISALLOW_COPY_AND_ASSIGN(RecordWriteStub);
};


// Trampoline stub to call into native code. To call safely into native code
// in the presence of compacting GC (which can move code objects) we need to
// keep the code which called into native pinned in the memory. Currently the
// simplest approach is to generate such stub early enough so it can never be
// moved by GC
class DirectCEntryStub: public PlatformCodeStub {
 public:
  explicit DirectCEntryStub(Isolate* isolate) : PlatformCodeStub(isolate) {}
  void GenerateCall(MacroAssembler* masm, AsmRegister target);

 private:
  bool NeedsImmovableCode() override { return true; }

  DEFINE_NULL_CALL_INTERFACE_DESCRIPTOR();
  DEFINE_PLATFORM_CODE_STUB(DirectCEntry, PlatformCodeStub);
};


class NameDictionaryLookupStub: public PlatformCodeStub {
 public:
  enum LookupMode { POSITIVE_LOOKUP, NEGATIVE_LOOKUP };

  NameDictionaryLookupStub(Isolate* isolate, LookupMode mode)
      : PlatformCodeStub(isolate) {
    minor_key_ = LookupModeBits::encode(mode);
  }

  static void GenerateNegativeLookup(MacroAssembler* masm, Label* miss,
                                     Label* done, AsmRegister receiver,
                                     AsmRegister properties, Handle<Name> name,
                                     AsmRegister scratch0);

  bool SometimesSetsUpAFrame() override { return false; }

 private:
  static const int kInlinedProbes = 4;
  static const int kTotalProbes = 20;

  static const int kCapacityOffset =
      NameDictionary::kHeaderSize +
      NameDictionary::kCapacityIndex * kPointerSize;

  static const int kElementsStartOffset =
      NameDictionary::kHeaderSize +
      NameDictionary::kElementsStartIndex * kPointerSize;

  LookupMode mode() const { return LookupModeBits::decode(minor_key_); }

  class LookupModeBits: public BitField<LookupMode, 0, 1> {};

  DEFINE_NULL_CALL_INTERFACE_DESCRIPTOR();
  DEFINE_PLATFORM_CODE_STUB(NameDictionaryLookup, PlatformCodeStub);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_ARM_CODE_STUBS_ARM_H_
