// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains implementations of a few macros that are defined
// as external in Torque, so that generated debug code can work.

#ifndef V8_TORQUE_DEBUG_MACRO_SHIMS_H_
#define V8_TORQUE_DEBUG_MACRO_SHIMS_H_

#include "src/objects/smi.h"

namespace v8 {
namespace internal {
namespace debug_helper_internal {
namespace TorqueDebugMacroShims {
namespace CodeStubAssembler {

inline Value<intptr_t> ChangeInt32ToIntPtr(d::MemoryAccessor accessor,
                                           int32_t i) {
  return {d::MemoryAccessResult::kOk, i};
}
inline Value<uintptr_t> ChangeUint32ToWord(d::MemoryAccessor accessor,
                                           uint32_t u) {
  return {d::MemoryAccessResult::kOk, u};
}
inline Value<intptr_t> IntPtrAdd(d::MemoryAccessor accessor, intptr_t a,
                                 intptr_t b) {
  return {d::MemoryAccessResult::kOk, a + b};
}
inline Value<intptr_t> IntPtrMul(d::MemoryAccessor accessor, intptr_t a,
                                 intptr_t b) {
  return {d::MemoryAccessResult::kOk, a * b};
}
inline Value<intptr_t> Signed(d::MemoryAccessor accessor, uintptr_t u) {
  return {d::MemoryAccessResult::kOk, static_cast<intptr_t>(u)};
}
inline Value<int32_t> SmiUntag(d::MemoryAccessor accessor, uintptr_t s_t) {
  Smi s(s_t);
  return {d::MemoryAccessResult::kOk, s.value()};
}

}  // namespace CodeStubAssembler
}  // namespace TorqueDebugMacroShims
}  // namespace debug_helper_internal
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_DEBUG_MACRO_SHIMS_H_
