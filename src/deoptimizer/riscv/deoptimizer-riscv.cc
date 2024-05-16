// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/deoptimizer/deoptimizer.h"

namespace v8 {
namespace internal {

const int Deoptimizer::kEagerDeoptExitSize = 2 * kInstrSize;
const int Deoptimizer::kLazyDeoptExitSize = 2 * kInstrSize;

Float32 RegisterValues::GetFloatRegister(unsigned n) const {
  double double_val = base::ReadUnalignedValue<Float64>(
                          reinterpret_cast<Address>(simd128_registers_ + n))
                          .get_scalar();
  float float_val = static_cast<float>(double_val);
  return Float32::FromBits(base::bit_cast<uint32_t>(float_val));
}

Float64 RegisterValues::GetDoubleRegister(unsigned n) const {
  return base::ReadUnalignedValue<Float64>(
      reinterpret_cast<Address>(simd128_registers_ + n));
}

void FrameDescription::SetCallerPc(unsigned offset, intptr_t value) {
  SetFrameSlot(offset, value);
}

void FrameDescription::SetCallerFp(unsigned offset, intptr_t value) {
  SetFrameSlot(offset, value);
}

void FrameDescription::SetCallerConstantPool(unsigned offset, intptr_t value) {
  // No embedded constant pool support.
  UNREACHABLE();
}

void FrameDescription::SetPc(intptr_t pc) { pc_ = pc; }

}  // namespace internal
}  // namespace v8
