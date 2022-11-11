// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_BASE_STACK_H_
#define V8_HEAP_BASE_STACK_H_

#include <memory>

#include "src/base/macros.h"
#include "src/base/platform/platform.h"

namespace heap::base {

class StackVisitor {
 public:
  virtual ~StackVisitor() = default;
  virtual void VisitPointer(const void* address) = 0;
};

// Abstraction over the stack. Supports handling of:
// - native stack;
// - ASAN/MSAN;
// - SafeStack: https://releases.llvm.org/10.0.0/tools/clang/docs/SafeStack.html
//
// TODO(chromium:1056170): Consider adding a component that keeps track
// of relevant GC stack regions where interesting pointers can be found.
class V8_EXPORT_PRIVATE Stack final {
 public:
  // The following constant is architecture-specific. The size of the buffer
  // for storing the callee-saved registers is going to be equal to
  // NumberOfCalleeSavedRegisters * sizeof(intptr_t).

#if V8_HOST_ARCH_IA32
  // Must be consistent with heap/base/asm/ia32/.
  static constexpr int NumberOfCalleeSavedRegisters = 3;
#elif V8_HOST_ARCH_X64
#ifdef _WIN64
  // Must be consistent with heap/base/asm/x64/.
  static constexpr int NumberOfCalleeSavedRegisters = 28;
#else   // !_WIN64
  // Must be consistent with heap/base/asm/x64/.
  static constexpr int NumberOfCalleeSavedRegisters = 5;
#endif  // !_WIN64
#elif V8_HOST_ARCH_ARM64
  // Must be consistent with heap/base/asm/arm64/.
  static constexpr int NumberOfCalleeSavedRegisters = 11;
#elif V8_HOST_ARCH_ARM
  // Must be consistent with heap/base/asm/arm/.
  static constexpr int NumberOfCalleeSavedRegisters = 8;
#elif V8_HOST_ARCH_PPC64
  // Must be consistent with heap/base/asm/ppc/.
  static constexpr int NumberOfCalleeSavedRegisters = 20;
#elif V8_HOST_ARCH_PPC
  // Must be consistent with heap/base/asm/ppc/.
  static constexpr int NumberOfCalleeSavedRegisters = 20;
#elif V8_HOST_ARCH_MIPS64
  // Must be consistent with heap/base/asm/mips64el/.
  static constexpr int NumberOfCalleeSavedRegisters = 9;
#elif V8_HOST_ARCH_LOONG64
  // Must be consistent with heap/base/asm/loong64/.
  static constexpr int NumberOfCalleeSavedRegisters = 11;
#elif V8_HOST_ARCH_S390
  // Must be consistent with heap/base/asm/s390/.
  static constexpr int NumberOfCalleeSavedRegisters = 10;
#elif V8_HOST_ARCH_RISCV32
  // Must be consistent with heap/base/asm/riscv/.
  static constexpr int NumberOfCalleeSavedRegisters = 12;
#elif V8_HOST_ARCH_RISCV64
  // Must be consistent with heap/base/asm/riscv/.
  static constexpr int NumberOfCalleeSavedRegisters = 12;
#else
#error Unknown architecture.
#endif

  explicit Stack(const void* stack_start = nullptr);

  // Returns true if the stack is used.
  bool IsUsed() const { return stack_start_ != nullptr; }

  // Sets the start of the stack.
  void SetStackStart(const void* stack_start);

  // Returns true if |slot| is part of the stack and false otherwise.
  bool IsOnStack(const void* slot) const;

  // Word-aligned iteration of the stack and the saved registers.
  // Slot values are passed on to `visitor`.
  V8_NOINLINE void IteratePointers(StackVisitor* visitor) const;

  // Saves and clears the stack context, i.e., it sets the stack marker and
  // saves the registers.
  void SaveContext();
  void ClearContext();

 private:
  const void* stack_start_;

#ifdef V8_ENABLE_CONSERVATIVE_STACK_SCANNING
  struct Context {
    int nesting_counter_ = 0;
    const void* stack_marker_ = nullptr;
    // We always double-align this buffer, to support for longer registers,
    // e.g., 128-bit registers in WIN64.
    alignas(2 * sizeof(intptr_t))
        std::array<intptr_t, NumberOfCalleeSavedRegisters> registers_;
  };

  std::unique_ptr<Context> context_;
#endif  // V8_ENABLE_CONSERVATIVE_STACK_SCANNING
};

}  // namespace heap::base

#endif  // V8_HEAP_BASE_STACK_H_
