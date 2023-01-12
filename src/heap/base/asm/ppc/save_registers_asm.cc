// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/heap/base/stack.h>

// Save all callee-saved registers in the specified buffer.
// extern "C" void SaveCalleeSavedRegisters(intptr_t* buffer);

// See asm/x64/save_registers_asm.cc for why the function is not generated
// using clang.
//
// Do not depend on V8_TARGET_OS_* defines as some embedders may override the
// GN toolchain (e.g. ChromeOS) and not provide them.
//
// PPC ABI source:
// http://refspecs.linuxfoundation.org/ELF/ppc64/PPC-elf64abi.html

// AIX Runtime process stack:
// https://www.ibm.com/support/knowledgecenter/ssw_aix_71/assembler/idalangref_runtime_process.html

#ifdef __PPC64__

// 20 64-bit registers = 20 intprt_t
static_assert(heap::base::Stack::NumberOfCalleeSavedRegisters() == 20,
              "Mismatch in the number of callee-saved registers");
static_assert(sizeof(intptr_t) == 8, "Mismatch in word size");

asm(
#if defined(_AIX)
    ".csect .text[PR]                            \n"
    ".align 2                                    \n"
    ".globl .SaveCalleeSavedRegisters, hidden    \n"
    ".SaveCalleeSavedRegisters:                  \n"
#else
    ".text                                       \n"
    ".align 2                                    \n"
    ".globl SaveCalleeSavedRegisters             \n"
    ".type SaveCalleeSavedRegisters, %function   \n"
    ".hidden SaveCalleeSavedRegisters            \n"
    "SaveCalleeSavedRegisters:                   \n"
#endif
    // r3: [ intptr_t* buffer ]
    // Save the callee-saved registers: lr, TOC pointer (r2), r14-r31.
    "  mflr 0                                    \n"
    "  std 0, 0(3)                               \n"
    "  std 2, 8(3)                               \n"
    "  std 14, 16(3)                             \n"
    "  std 15, 24(3)                             \n"
    "  std 16, 32(3)                             \n"
    "  std 17, 40(3)                             \n"
    "  std 18, 48(3)                             \n"
    "  std 19, 56(3)                             \n"
    "  std 20, 64(3)                             \n"
    "  std 21, 72(3)                             \n"
    "  std 22, 80(3)                             \n"
    "  std 23, 88(3)                             \n"
    "  std 24, 96(3)                             \n"
    "  std 25, 104(3)                            \n"
    "  std 26, 112(3)                            \n"
    "  std 27, 120(3)                            \n"
    "  std 28, 128(3)                            \n"
    "  std 29, 136(3)                            \n"
    "  std 30, 144(3)                            \n"
    "  std 31, 152(3)                            \n"
    // Return.
    "  blr                                       \n");

#else  // !__PPC64__

// 20 32-bit registers = 20 intprt_t
static_assert(heap::base::Stack::NumberOfCalleeSavedRegisters() == 20,
              "Mismatch in the number of callee-saved registers");
static_assert(sizeof(intptr_t) == 4, "Mismatch in word size");

asm(
#if defined(_AIX)
    ".globl .SaveCalleeSavedRegisters, hidden    \n"
    ".csect .text[PR]                            \n"
    ".SaveCalleeSavedRegisters:                  \n"
#else
    ".globl SaveCalleeSavedRegisters             \n"
    ".type SaveCalleeSavedRegisters, %function   \n"
    ".hidden SaveCalleeSavedRegisters            \n"
    "SaveCalleeSavedRegisters:                   \n"
#endif
    // r3: [ intptr_t* buffer ]
    // Save the callee-saved registers: lr, TOC pointer (r2), r14-r31.
    "  mflr 0                                    \n"
    "  st 0, 0(3)                                \n"
    "  st 2, 4(3)                                \n"
    "  st 14, 8(3)                               \n"
    "  st 15, 12(3)                              \n"
    "  st 16, 16(3)                              \n"
    "  st 17, 20(3)                              \n"
    "  st 18, 24(3)                              \n"
    "  st 19, 28(3)                              \n"
    "  st 20, 32(3)                              \n"
    "  st 21, 36(3)                              \n"
    "  st 22, 40(3)                              \n"
    "  st 23, 44(3)                              \n"
    "  st 24, 48(3)                              \n"
    "  st 25, 52(3)                              \n"
    "  st 26, 56(3)                              \n"
    "  st 27, 60(3)                              \n"
    "  st 28, 64(3)                              \n"
    "  st 29, 68(3)                              \n"
    "  st 30, 72(3)                              \n"
    "  st 31, 76(3)                              \n"
    // Return.
    "  blr                                       \n");

#endif  // __PPC64__
