// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arguments.h"

#if !(V8_HOST_ARCH_ARM || V8_HOST_ARCH_ARM64 || V8_HOST_ARCH_IA32 || \
      V8_HOST_ARCH_X64)
double CobberDoubleRegisterImpl(double x1, double x2, double x3, double x4) {
  return x1 * 1.01 + x2 * 2.02 + x3 * 3.03 + x4 * 4.04;
}
#endif

namespace v8 {
namespace internal {

void ClobberDoubleRegisters() {
#if V8_HOST_ARCH_ARM
// copy from "src/codegen/arm/register-arm.h"
#define HOST_DOUBLE_REGISTERS(V)                                               \
  V(d0)                                                                        \
  V(d1)                                                                        \
  V(d2)                                                                        \
  V(d3)                                                                        \
  V(d4)                                                                        \
  V(d5)                                                                        \
  V(d6)                                                                        \
  V(d7)                                                                        \
  V(d8)                                                                        \
  V(d9)                                                                        \
  V(d10)                                                                       \
  V(d11)                                                                       \
  V(d12)                                                                       \
  V(d13)                                                                       \
  V(d14)                                                                       \
  V(d15) V(d16) V(d17) V(d18) V(d19) V(d20) V(d21) V(d22) V(d23) V(d24) V(d25) \
      V(d26) V(d27) V(d28) V(d29) V(d30) V(d31)
#elif V8_HOST_ARCH_ARM64
// copy from "src/codegen/arm64/register-arm64.h"
#define HOST_DOUBLE_REGISTERS(R)                                               \
  R(d0)                                                                        \
  R(d1)                                                                        \
  R(d2)                                                                        \
  R(d3)                                                                        \
  R(d4)                                                                        \
  R(d5)                                                                        \
  R(d6)                                                                        \
  R(d7)                                                                        \
  R(d8)                                                                        \
  R(d9)                                                                        \
  R(d10)                                                                       \
  R(d11)                                                                       \
  R(d12)                                                                       \
  R(d13)                                                                       \
  R(d14)                                                                       \
  R(d15) R(d16) R(d17) R(d18) R(d19) R(d20) R(d21) R(d22) R(d23) R(d24) R(d25) \
      R(d26) R(d27) R(d28) R(d29) R(d30) R(d31)
#elif V8_HOST_ARCH_IA32
// copy from "src/codegen/ia32/register-ia32.h"
#define HOST_DOUBLE_REGISTERS(V) \
  V(xmm0)                        \
  V(xmm1)                        \
  V(xmm2)                        \
  V(xmm3)                        \
  V(xmm4)                        \
  V(xmm5)                        \
  V(xmm6)                        \
  V(xmm7)
#elif V8_HOST_ARCH_X64
// copy from "src/codegen/x64/register-x64.h"
#define HOST_DOUBLE_REGISTERS(V) \
  V(xmm0)                        \
  V(xmm1)                        \
  V(xmm2)                        \
  V(xmm3)                        \
  V(xmm4)                        \
  V(xmm5)                        \
  V(xmm6)                        \
  V(xmm7)                        \
  V(xmm8)                        \
  V(xmm9)                        \
  V(xmm10)                       \
  V(xmm11)                       \
  V(xmm12)                       \
  V(xmm13)                       \
  V(xmm14)                       \
  V(xmm15)

#endif

// clobber all double registers
#if V8_HOST_ARCH_X64 || V8_HOST_ARCH_IA32
#define CLOBBER_REGISTER(R) \
  __asm__ volatile(         \
      "xorps "              \
      "%%" #R               \
      ","                   \
      "%%" #R ::            \
          :);
  HOST_DOUBLE_REGISTERS(CLOBBER_REGISTER)
#undef CLOBBER_REGISTER
#undef HOST_DOUBLE_REGISTERS

#elif V8_HOST_ARCH_ARM || V8_HOST_ARCH_ARM64
#define CLOBBER_REGISTER(R) \
  __asm__ volatile(         \
      "eor "                \
      "" #R "," #R          \
      ","                   \
      "" #R ::              \
          :);
  HOST_DOUBLE_REGISTERS(CLOBBER_REGISTER)
#undef CLOBBER_REGISTER
#undef HOST_DOUBLE_REGISTERS

#elif V8_HOST_ARCH_MIPS || V8_HOST_ARCH_MIPS64
  ClobberDoubleRegistersImpl(1, 2, 3, 4);
#elif V8_HOST_ARCH_PPC || V8_HOST_ARCH_PPC64
  ClobberDoubleRegistersImpl(1, 2, 3, 4);
#elif V8_HOST_ARCH_LOONG64
  ClobberDoubleRegistersImpl(1, 2, 3, 4);
#elif V8_HOST_ARCH_S390
  ClobberDoubleRegistersImpl(1, 2, 3, 4);
#elif V8_HOST_ARCH_RISCV64
  ClobberDoubleRegistersImpl(1, 2, 3, 4);
#endif
}

}  // namespace internal
}  // namespace v8
