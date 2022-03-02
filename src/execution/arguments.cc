// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arguments.h"

#if V8_HOST_ARCH_ARM
#include "src/codegen/arm/register-arm.h"
#elif V8_HOST_ARCH_ARM64
#include "src/codegen/arm64/register-arm64.h"
#elif V8_HOST_ARCH_IA32
#include "src/codegen/ia32/register-ia32.h"
#elif V8_HOST_ARCH_X64
#include "src/codegen/x64/register-x64.h"
#endif

namespace v8 {
namespace internal {

double ClobberDoubleRegisters() {
// clobber all double registers
#if V8_HOST_ARCH_X64 || V8_HOST_ARCH_IA32
#define CLOBBER_REGISTER(R) \
  __asm__ volatile(         \
      "xorps "              \
      "%%" #R               \
      ","                   \
      "%%" #R ::            \
          :);
  DOUBLE_REGISTERS(CLOBBER_REGISTER)
#undef CLOBBER_REGISTER
  return 0;

#elif V8_HOST_ARCH_ARM || V8_HOST_ARCH_ARM64
#define CLOBBER_REGISTER(R) \
  __asm__ volatile(         \
      "eor "                \
      "" #R "," #R          \
      ","                   \
      "" #R ::              \
          :);
  DOUBLE_REGISTERS(CLOBBER_REGISTER)
#undef CLOBBER_REGISTER
  return 0;

#else
  return 1 * 1.01 + 2 * 2.02 + 3 * 3.03 + 4 * 4.04;
#endif
}

}  // namespace internal
}  // namespace v8
