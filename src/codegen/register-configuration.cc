// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/register-configuration.h"
#include "src/base/lazy-instance.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/register-arch.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

namespace {

#define REGISTER_COUNT(R) 1 +
static const int kMaxAllocatableGeneralRegisterCount =
    ALLOCATABLE_GENERAL_REGISTERS(REGISTER_COUNT) 0;
static const int kMaxAllocatableDoubleRegisterCount =
    ALLOCATABLE_DOUBLE_REGISTERS(REGISTER_COUNT) 0;

static const int kAllocatableGeneralCodes[] = {
#define REGISTER_CODE(R) kRegCode_##R,
    ALLOCATABLE_GENERAL_REGISTERS(REGISTER_CODE)};
#undef REGISTER_CODE

#define REGISTER_CODE(R) kDoubleCode_##R,
static const int kAllocatableDoubleCodes[] = {
    ALLOCATABLE_DOUBLE_REGISTERS(REGISTER_CODE)};
#if V8_TARGET_ARCH_ARM
static const int kAllocatableNoVFP32DoubleCodes[] = {
    ALLOCATABLE_NO_VFP32_DOUBLE_REGISTERS(REGISTER_CODE)};
#endif  // V8_TARGET_ARCH_ARM
#undef REGISTER_CODE

STATIC_ASSERT(RegisterConfiguration::kMaxGeneralRegisters >=
              Register::kNumRegisters);
STATIC_ASSERT(RegisterConfiguration::kMaxFPRegisters >=
              FloatRegister::kNumRegisters);
STATIC_ASSERT(RegisterConfiguration::kMaxFPRegisters >=
              DoubleRegister::kNumRegisters);
STATIC_ASSERT(RegisterConfiguration::kMaxFPRegisters >=
              Simd128Register::kNumRegisters);

static int get_num_allocatable_double_registers() {
  return
#if V8_TARGET_ARCH_IA32
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_X64
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_ARM
      CpuFeatures::IsSupported(VFP32DREGS)
          ? kMaxAllocatableDoubleRegisterCount
          : (ALLOCATABLE_NO_VFP32_DOUBLE_REGISTERS(REGISTER_COUNT) 0);
#elif V8_TARGET_ARCH_ARM64
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_MIPS
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_MIPS64
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_PPC
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_PPC64
      kMaxAllocatableDoubleRegisterCount;
#elif V8_TARGET_ARCH_S390
      kMaxAllocatableDoubleRegisterCount;
#else
#error Unsupported target architecture.
#endif
}

#undef REGISTER_COUNT

static const int* get_allocatable_double_codes() {
  return
#if V8_TARGET_ARCH_ARM
      CpuFeatures::IsSupported(VFP32DREGS) ? kAllocatableDoubleCodes
                                           : kAllocatableNoVFP32DoubleCodes;
#else
      kAllocatableDoubleCodes;
#endif
}

static int get_simplified_aliasing_midpoint() {
  int num_double_regs = get_num_allocatable_double_registers();
  const int* double_reg_codes = get_allocatable_double_codes();
  int mid_point = num_double_regs / 2;
  while (double_reg_codes[mid_point] * 2 >
         RegisterConfiguration::kMaxFPRegisters) {
    mid_point--;
  }
  return mid_point;
}

class ArchDefaultRegisterConfiguration : public RegisterConfiguration {
 public:
  ArchDefaultRegisterConfiguration()
      : ArchDefaultRegisterConfiguration(
            get_num_allocatable_double_registers(),
            get_allocatable_double_codes(),
            kSimpleFPAliasing ? AliasingKind::OVERLAP : AliasingKind::COMBINE) {
  }

 protected:
  ArchDefaultRegisterConfiguration(int num_allocatable_double_registers,
                                   const int* allocatable_double_code,
                                   AliasingKind aliasing_kind)
      : RegisterConfiguration(
            Register::kNumRegisters, DoubleRegister::kNumRegisters,
            kMaxAllocatableGeneralRegisterCount,
            num_allocatable_double_registers, kAllocatableGeneralCodes,
            allocatable_double_code, aliasing_kind) {}
};

DEFINE_LAZY_LEAKY_OBJECT_GETTER(ArchDefaultRegisterConfiguration,
                                GetDefaultRegisterConfiguration)

// Allocatable registers with the masking register removed.
class ArchDefaultPoisoningRegisterConfiguration : public RegisterConfiguration {
 public:
  ArchDefaultPoisoningRegisterConfiguration()
      : ArchDefaultPoisoningRegisterConfiguration(
            get_num_allocatable_double_registers(),
            get_allocatable_double_codes(),
            kSimpleFPAliasing ? AliasingKind::OVERLAP : AliasingKind::COMBINE) {
  }

 protected:
  ArchDefaultPoisoningRegisterConfiguration(
      int num_allocatable_double_registers, const int* allocatable_double_code,
      AliasingKind aliasing_kind)
      : RegisterConfiguration(
            Register::kNumRegisters, DoubleRegister::kNumRegisters,
            kMaxAllocatableGeneralRegisterCount - 1,
            num_allocatable_double_registers, InitializeGeneralRegisterCodes(),
            allocatable_double_code, aliasing_kind) {}

 private:
  static const int* InitializeGeneralRegisterCodes() {
    int filtered_index = 0;
    for (int i = 0; i < kMaxAllocatableGeneralRegisterCount; ++i) {
      if (kAllocatableGeneralCodes[i] != kSpeculationPoisonRegister.code()) {
        allocatable_general_codes_[filtered_index] =
            kAllocatableGeneralCodes[i];
        filtered_index++;
      }
    }
    DCHECK_EQ(filtered_index, kMaxAllocatableGeneralRegisterCount - 1);
    return allocatable_general_codes_;
  }

  static int
      allocatable_general_codes_[kMaxAllocatableGeneralRegisterCount - 1];
};

int ArchDefaultPoisoningRegisterConfiguration::allocatable_general_codes_
    [kMaxAllocatableGeneralRegisterCount - 1];

DEFINE_LAZY_LEAKY_OBJECT_GETTER(ArchDefaultPoisoningRegisterConfiguration,
                                GetDefaultPoisoningRegisterConfiguration)

// Allocatable registers with the masking register and simplified floating point
// aliasing to avoid AliasingKind::COMBINE complexity.
template <class Base>
class SimpleFpRegisterConfiguration : public Base {
 public:
  SimpleFpRegisterConfiguration()
      : Base(GetDoubleRegisterCount(), GetDoubleRegisterCodes(),
             kSimpleFPAliasing ? Base::AliasingKind::OVERLAP
                               : Base::AliasingKind::SIMPLIFY) {}

 private:
  static bool double_code_for_simd128_register(int index, int double_reg_count,
                                               const int* double_reg_codes) {
    DCHECK_LT(index, double_reg_count);
    int code = double_reg_codes[index];
    // Only even register codes match a simd128 register.
    if (code % 2 != 0) return false;

    // Return true if the next allocatable register is an adjacent code.
    return index + 1 < double_reg_count &&
           double_reg_codes[index + 1] == code + 1;
  }

  static void InitializeDoubleRegisterCodes() {
    if (num_allocatable_double_codes_ != 0) return;

    int double_reg_count = get_num_allocatable_double_registers();
    const int* double_reg_codes = get_allocatable_double_codes();
    int float_to_simd_boundary = get_simplified_aliasing_midpoint();
    DCHECK_LT(float_to_simd_boundary, double_reg_count);

    for (int i = 0; i < float_to_simd_boundary; ++i) {
      // For codes below the float_to_simd_boundary, add each code (each of
      // which will map to a signle float register).
      int code = double_reg_codes[i];
      DCHECK_LT(code * 2, Base::kMaxFPRegisters);
      allocatable_double_codes_[num_allocatable_double_codes_++] = code;
    }

    for (int i = float_to_simd_boundary; i < double_reg_count; ++i) {
      // For codes above the float_to_simd_boundary, add every even code that
      // has an allocatable adjacent code (each of which will map to a
      // simd_128 register).
      int code = double_reg_codes[i];
      if (double_code_for_simd128_register(i, double_reg_count,
                                           double_reg_codes)) {
        allocatable_double_codes_[num_allocatable_double_codes_++] = code;
      }
    }
  }

  static const int* GetDoubleRegisterCodes() {
    InitializeDoubleRegisterCodes();
    return allocatable_double_codes_;
  }

  static int GetDoubleRegisterCount() {
    InitializeDoubleRegisterCodes();
    return num_allocatable_double_codes_;
  }

  static int allocatable_double_codes_[kMaxAllocatableDoubleRegisterCount];
  static int num_allocatable_double_codes_;
};

template <class Base>
int SimpleFpRegisterConfiguration<
    Base>::allocatable_double_codes_[kMaxAllocatableDoubleRegisterCount];
template <class Base>
int SimpleFpRegisterConfiguration<Base>::num_allocatable_double_codes_ = 0;

class ArchDefaultSimpleFpRegisterConfiguration
    : public SimpleFpRegisterConfiguration<ArchDefaultRegisterConfiguration> {
 public:
  ArchDefaultSimpleFpRegisterConfiguration()
      : SimpleFpRegisterConfiguration<ArchDefaultRegisterConfiguration>() {}
};

DEFINE_LAZY_LEAKY_OBJECT_GETTER(ArchDefaultSimpleFpRegisterConfiguration,
                                GetDefaultSimpleFpRegisterConfiguration)

class ArchDefaultPoisoningSimpleFpRegisterConfiguration
    : public SimpleFpRegisterConfiguration<
          ArchDefaultPoisoningRegisterConfiguration> {
 public:
  ArchDefaultPoisoningSimpleFpRegisterConfiguration()
      : SimpleFpRegisterConfiguration<
            ArchDefaultPoisoningRegisterConfiguration>() {}
};

DEFINE_LAZY_LEAKY_OBJECT_GETTER(
    ArchDefaultPoisoningSimpleFpRegisterConfiguration,
    GetDefaultPoisoningSimpleFpRegisterConfiguration)

// RestrictedRegisterConfiguration uses the subset of allocatable general
// registers the architecture support, which results into generating assembly
// to use less registers. Currently, it's only used by RecordWrite code stub.
class RestrictedRegisterConfiguration : public RegisterConfiguration {
 public:
  RestrictedRegisterConfiguration(
      int num_allocatable_general_registers,
      std::unique_ptr<int[]> allocatable_general_register_codes,
      std::unique_ptr<char const*[]> allocatable_general_register_names)
      : RegisterConfiguration(
            Register::kNumRegisters, DoubleRegister::kNumRegisters,
            num_allocatable_general_registers,
            get_num_allocatable_double_registers(),
            allocatable_general_register_codes.get(),
            get_allocatable_double_codes(),
            kSimpleFPAliasing ? AliasingKind::OVERLAP : AliasingKind::COMBINE),
        allocatable_general_register_codes_(
            std::move(allocatable_general_register_codes)),
        allocatable_general_register_names_(
            std::move(allocatable_general_register_names)) {
    for (int i = 0; i < num_allocatable_general_registers; ++i) {
      DCHECK(
          IsAllocatableGeneralRegister(allocatable_general_register_codes_[i]));
    }
  }

  bool IsAllocatableGeneralRegister(int code) {
    for (int i = 0; i < kMaxAllocatableGeneralRegisterCount; ++i) {
      if (code == kAllocatableGeneralCodes[i]) {
        return true;
      }
    }
    return false;
  }

 private:
  std::unique_ptr<int[]> allocatable_general_register_codes_;
  std::unique_ptr<char const*[]> allocatable_general_register_names_;
};

}  // namespace

const RegisterConfiguration* RegisterConfiguration::Default() {
  return GetDefaultRegisterConfiguration();
}

const RegisterConfiguration* RegisterConfiguration::Poisoning() {
  return GetDefaultPoisoningRegisterConfiguration();
}

const RegisterConfiguration* RegisterConfiguration::SimpleFp() {
  return GetDefaultSimpleFpRegisterConfiguration();
}

const RegisterConfiguration* RegisterConfiguration::PoisoningSimpleFp() {
  return GetDefaultPoisoningSimpleFpRegisterConfiguration();
}

const RegisterConfiguration* RegisterConfiguration::RestrictGeneralRegisters(
    RegList registers) {
  int num = NumRegs(registers);
  std::unique_ptr<int[]> codes{new int[num]};
  std::unique_ptr<char const* []> names { new char const*[num] };
  int counter = 0;
  for (int i = 0; i < Default()->num_allocatable_general_registers(); ++i) {
    auto reg = Register::from_code(Default()->GetAllocatableGeneralCode(i));
    if (reg.bit() & registers) {
      DCHECK(counter < num);
      codes[counter] = reg.code();
      names[counter] = RegisterName(Register::from_code(i));
      counter++;
    }
  }

  return new RestrictedRegisterConfiguration(num, std::move(codes),
                                             std::move(names));
}

RegisterConfiguration::RegisterConfiguration(
    int num_general_registers, int num_double_registers,
    int num_allocatable_general_registers, int num_allocatable_double_registers,
    const int* allocatable_general_codes, const int* allocatable_double_codes,
    AliasingKind fp_aliasing_kind)
    : num_general_registers_(num_general_registers),
      num_float_registers_(0),
      num_double_registers_(num_double_registers),
      num_simd128_registers_(0),
      num_allocatable_general_registers_(num_allocatable_general_registers),
      num_allocatable_float_registers_(0),
      num_allocatable_double_registers_(num_allocatable_double_registers),
      num_allocatable_simd128_registers_(0),
      allocatable_general_codes_mask_(0),
      allocatable_float_codes_mask_(0),
      allocatable_double_codes_mask_(0),
      allocatable_simd128_codes_mask_(0),
      allocatable_general_codes_(allocatable_general_codes),
      allocatable_double_codes_(allocatable_double_codes),
      fp_aliasing_kind_(fp_aliasing_kind) {
  DCHECK_LE(num_general_registers_,
            RegisterConfiguration::kMaxGeneralRegisters);
  DCHECK_LE(num_double_registers_, RegisterConfiguration::kMaxFPRegisters);
  for (int i = 0; i < num_allocatable_general_registers_; ++i) {
    allocatable_general_codes_mask_ |= (1 << allocatable_general_codes_[i]);
  }
  for (int i = 0; i < num_allocatable_double_registers_; ++i) {
    allocatable_double_codes_mask_ |= (1 << allocatable_double_codes_[i]);
  }

  if (fp_aliasing_kind_ == COMBINE) {
    num_float_registers_ = num_double_registers_ * 2 <= kMaxFPRegisters
                               ? num_double_registers_ * 2
                               : kMaxFPRegisters;
    num_allocatable_float_registers_ = 0;
    for (int i = 0; i < num_allocatable_double_registers_; i++) {
      int base_code = allocatable_double_codes_[i] * 2;
      if (base_code >= kMaxFPRegisters) continue;
      allocatable_float_codes_[num_allocatable_float_registers_++] = base_code;
      allocatable_float_codes_[num_allocatable_float_registers_++] =
          base_code + 1;
      allocatable_float_codes_mask_ |= (0x3 << base_code);
    }
    num_simd128_registers_ = num_double_registers_ / 2;
    num_allocatable_simd128_registers_ = 0;
    int last_simd128_code = allocatable_double_codes_[0] / 2;
    for (int i = 1; i < num_allocatable_double_registers_; i++) {
      int next_simd128_code = allocatable_double_codes_[i] / 2;
      // This scheme assumes allocatable_double_codes_ are strictly increasing.
      DCHECK_GE(next_simd128_code, last_simd128_code);
      if (last_simd128_code == next_simd128_code) {
        allocatable_simd128_codes_[num_allocatable_simd128_registers_++] =
            next_simd128_code;
        allocatable_simd128_codes_mask_ |= (0x1 << next_simd128_code);
      }
      last_simd128_code = next_simd128_code;
    }
  } else if (fp_aliasing_kind_ == SIMPLIFY) {
    int float_to_simd_boundary = get_simplified_aliasing_midpoint();
    DCHECK_LT(float_to_simd_boundary, num_allocatable_double_registers_);
    num_float_registers_ = num_double_registers_ * 2 <= kMaxFPRegisters
                               ? num_double_registers_ * 2
                               : kMaxFPRegisters;
    num_allocatable_float_registers_ = 0;
    for (int i = 0; i < float_to_simd_boundary; ++i) {
      int float_code = allocatable_double_codes_[i] * 2;
      DCHECK_LT(float_code, kMaxFPRegisters);
      allocatable_float_codes_[num_allocatable_float_registers_++] = float_code;
      allocatable_float_codes_mask_ |= (0x1 << float_code);
    }

    num_simd128_registers_ = num_double_registers_ / 2;
    num_allocatable_simd128_registers_ = 0;
    for (int i = float_to_simd_boundary; i < num_allocatable_double_registers_;
         ++i) {
      int simd128_code = allocatable_double_codes_[i] / 2;
      allocatable_simd128_codes_[num_allocatable_simd128_registers_++] =
          simd128_code;
      allocatable_simd128_codes_mask_ |= (0x1 << simd128_code);
    }
  } else {
    DCHECK(fp_aliasing_kind_ == OVERLAP);
    num_float_registers_ = num_simd128_registers_ = num_double_registers_;
    num_allocatable_float_registers_ = num_allocatable_simd128_registers_ =
        num_allocatable_double_registers_;
    for (int i = 0; i < num_allocatable_float_registers_; ++i) {
      allocatable_float_codes_[i] = allocatable_simd128_codes_[i] =
          allocatable_double_codes_[i];
    }
    allocatable_float_codes_mask_ = allocatable_simd128_codes_mask_ =
        allocatable_double_codes_mask_;
  }
}

// Assert that kFloat32, kFloat64, and kSimd128 are consecutive values.
STATIC_ASSERT(static_cast<int>(MachineRepresentation::kSimd128) ==
              static_cast<int>(MachineRepresentation::kFloat64) + 1);
STATIC_ASSERT(static_cast<int>(MachineRepresentation::kFloat64) ==
              static_cast<int>(MachineRepresentation::kFloat32) + 1);

int RegisterConfiguration::GetFloatToSimd128TransitionIndex() const {
  DCHECK(fp_aliasing_kind_ == SIMPLIFY);
  return get_simplified_aliasing_midpoint();
}

int RegisterConfiguration::GetAliases(MachineRepresentation rep, int index,
                                      MachineRepresentation other_rep,
                                      int* alias_base_index) const {
  DCHECK(fp_aliasing_kind_ == COMBINE);
  DCHECK(IsFloatingPoint(rep) && IsFloatingPoint(other_rep));
  if (rep == other_rep) {
    *alias_base_index = index;
    return 1;
  }
  int rep_int = static_cast<int>(rep);
  int other_rep_int = static_cast<int>(other_rep);
  if (rep_int > other_rep_int) {
    int shift = rep_int - other_rep_int;
    int base_index = index << shift;
    if (base_index >= kMaxFPRegisters) {
      // Alias indices would be out of FP register range.
      return 0;
    }
    *alias_base_index = base_index;
    return 1 << shift;
  }
  int shift = other_rep_int - rep_int;
  *alias_base_index = index >> shift;
  return 1;
}

bool RegisterConfiguration::AreAliases(MachineRepresentation rep, int index,
                                       MachineRepresentation other_rep,
                                       int other_index) const {
  DCHECK(fp_aliasing_kind_ == COMBINE);
  DCHECK(IsFloatingPoint(rep) && IsFloatingPoint(other_rep));
  if (rep == other_rep) {
    return index == other_index;
  }
  int rep_int = static_cast<int>(rep);
  int other_rep_int = static_cast<int>(other_rep);
  if (rep_int > other_rep_int) {
    int shift = rep_int - other_rep_int;
    return index == other_index >> shift;
  }
  int shift = other_rep_int - rep_int;
  return index >> shift == other_index;
}

}  // namespace internal
}  // namespace v8
