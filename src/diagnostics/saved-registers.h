// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_SAVED_REGISTERS_H_
#define V8_DIAGNOSTICS_SAVED_REGISTERS_H_

namespace v8 {

#ifdef V8_TARGET_ARCH_ARM
struct CalleeSavedRegisters {
  void* arm_r4;
  void* arm_r5;
  void* arm_r6;
  void* arm_r7;
  void* arm_r8;
  void* arm_r9;
  void* arm_r10;
};
#else
struct CalleeSavedRegisters {};
#endif

}  // namespace v8

#endif  // V8_DIAGNOSTICS_SAVED_REGISTERS_H_
