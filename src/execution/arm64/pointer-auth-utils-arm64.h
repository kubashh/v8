// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_ARM64_POINTER_AUTH_UTILS_ARM64_H_
#define V8_EXECUTION_ARM64_POINTER_AUTH_UTILS_ARM64_H_

#include "src/execution/arm64/simulator-arm64.h"

namespace v8 {
namespace internal {
namespace pointer_auth {

using v8::internal::Isolate;
#ifdef USE_SIMULATOR
using v8::internal::Simulator;
#endif

// The following functions execute on the host and therefore need a different
// path based on whether we are simulating arm64 or not.

// clang-format off

// Sign {pc} using {sp}. This is a macro to ensure it gets inlined.
#ifdef USE_SIMULATOR
#define POINTER_AUTH_SIGN_PC_WITH_SP_IMPL(pc, sp)          \
do {                                                       \
  uint64_t _pc = pc;                                       \
  uint64_t _sp = sp;                                       \
  _pc = Simulator::AddPAC(_pc, _sp, Simulator::kPACKeyIA,  \
                          Simulator::kInstructionPointer); \
  pc = _pc;                                                \
} while (false)
#else
#define POINTER_AUTH_SIGN_PC_WITH_SP_IMPL(pc, sp)          \
do {                                                       \
  uint64_t _pc = pc;                                       \
  uint64_t _sp = sp;                                       \
  asm volatile("mov x17, %[pc]\n\t"                        \
               "mov x16, %[stack_ptr]\n\t"                 \
               "pacia1716\n\t"                             \
               "mov %[pc], x17\n\t"                        \
               : [pc] "+r" (_pc)                           \
               : [stack_ptr] "r" (_sp)                     \
               : "x16", "x17");                            \
  pc = _pc;                                                \
} while (false)
#endif

// Authenticate the address stored in {pc_address} and replace it with
// {new_pc}, after signing it. {offset_from_sp} is the offset between
// {pc_address} and the pointer used as a context for signing.
// We are using a macro here instead of an inline function, to avoid
// silently failing to inline this.
#ifdef USE_SIMULATOR
#define POINTER_AUTH_REPLACE_PC_IMPL(pc_address, new_pc_arg, offset_from_sp) \
do {                                                                         \
  Address* _pc_address = pc_address;                                         \
  uint64_t _sp = reinterpret_cast<uint64_t>(_pc_address) + offset_from_sp;   \
  uint64_t _old_pc = reinterpret_cast<uint64_t>(*_pc_address);               \
  CHECK_EQ(Simulator::AuthPAC(_old_pc, _sp, Simulator::kPACKeyIA,            \
                              Simulator::kInstructionPointer),               \
           Simulator::StripPAC(_old_pc, Simulator::kInstructionPointer));    \
  uint64_t _new_pc = Simulator::AddPAC(new_pc_arg, _sp,                      \
                                       Simulator::kPACKeyIA,                 \
                                       Simulator::kInstructionPointer);      \
  *_pc_address = _new_pc;                                                    \
} while (false)
#else
#define POINTER_AUTH_REPLACE_PC_IMPL(pc_address, new_pc_arg, offset_from_sp) \
do {                                                                         \
  Address* _pc_address = pc_address;                                         \
  uint64_t _sp = reinterpret_cast<uint64_t>(_pc_address) + offset_from_sp;   \
  uint64_t _old_pc = reinterpret_cast<uint64_t>(*_pc_address);               \
  uint64_t _new_pc = new_pc_arg;                                             \
  /* Only store newly signed address after we have verified that the old     \
     address is authenticated. */                                            \
  asm volatile("mov x17, %[new_pc]\n\t"                                      \
               "mov x16, %[stack_ptr]\n\t"                                   \
               "pacia1716\n\t"                                               \
               "mov %[new_pc], x17\n\t"                                      \
               "mov x17, %[old_pc]\n\t"                                      \
               "autia1716\n\t"                                               \
               "ldr xzr, [x17]\n\t"                                          \
               : [new_pc] "+&r" (_new_pc)                                    \
               : [stack_ptr] "r" (_sp),                                      \
                 [old_pc] "r" (_old_pc)                                      \
               : "x16", "x17");                                              \
  *_pc_address = _new_pc;                                                    \
} while (false)
#endif

// Authenticate the address stored in {pc_address} based on {old_context} and
// replace it with the same address signed with {new_context} instead.
// We are using a macro here instead of an inline function, to avoid
// silently failing to inline this.
#ifdef USE_SIMULATOR
#define POINTER_AUTH_REPLACE_CONTEXT_IMPL(                                    \
                pc_address, old_context, new_context)                         \
do {                                                                          \
  Address* _pc_address = reinterpret_cast<Address*>(pc_address);              \
  uint64_t _old_signed_pc = static_cast<uint64_t>(*_pc_address);              \
  uint64_t _raw_pc = Simulator::AuthPAC(_old_signed_pc, old_context,          \
                                    Simulator::kPACKeyIA,                     \
                                    Simulator::kInstructionPointer);          \
  CHECK_EQ(_raw_pc,                                                           \
           Simulator::StripPAC(_raw_pc, Simulator::kInstructionPointer));     \
  uint64_t _new_signed_pc = Simulator::AddPAC(_raw_pc, new_context,           \
                                              Simulator::kPACKeyIA,           \
                                              Simulator::kInstructionPointer);\
  *_pc_address = _new_signed_pc;                                              \
} while (false)
#else
#define POINTER_AUTH_REPLACE_CONTEXT_IMPL(                                    \
                pc_address, old_context, new_context)                         \
do {                                                                          \
  Address* _pc_address = reinterpret_cast<Address*>(pc_address);              \
  uint64_t _old_signed_pc = static_cast<uint64_t>(*_pc_address);              \
  uint64_t _old_context = old_context;                                        \
  uint64_t _new_context = new_context;                                        \
  uint64_t _new_pc;                                                           \
  /* Only store newly signed address after we have verified that the old      \
     address is authenticated. */                                             \
  asm volatile("mov x17, %[old_pc]\n\t"                                   \
               "mov x16, %[old_ctx]\n\t"                                  \
               "autia1716\n\t"                                            \
               "mov x16, %[new_ctx]\n\t"                                  \
               "pacia1716\n\t"                                            \
               "mov %[new_pc], x17\n\t"                                   \
               "mov x17, %[old_pc]\n\t"                                   \
               "mov x16, %[old_ctx]\n\t"                                  \
               "autia1716\n\t"                                            \
               "ldr xzr, [x17]\n\t"                                       \
               : [new_pc] "=&r" (_new_pc)                                 \
               : [old_pc] "r" (_old_signed_pc),                           \
                 [old_ctx] "r" (_old_context),                            \
                 [new_ctx] "r" (_new_context)                             \
               : "x16", "x17");                                           \
  *_pc_address = _new_pc;                                                 \
} while (false)
#endif

// Authenticate the address stored in {pc_address}. {offset_from_sp} is the
// offset between {pc_address} and the pointer used as a context for signing.
V8_INLINE Address AuthenticatePC_Impl(Address* pc_address,
                                      unsigned offset_from_sp) {
  uint64_t sp = reinterpret_cast<uint64_t>(pc_address) + offset_from_sp;
  uint64_t pc = reinterpret_cast<uint64_t>(*pc_address);
#ifdef USE_SIMULATOR
  pc = Simulator::AuthPAC(pc, sp, Simulator::kPACKeyIA,
                          Simulator::kInstructionPointer);
#else
  asm volatile("mov x17, %[pc]\n\t"
               "mov x16, %[stack_ptr]\n\t"
               "autia1716\n\t"
               "ldr xzr, [x17]\n\t"
               "mov %[pc], x17\n\t"
               : [pc] "+r" (pc)
               : [stack_ptr] "r" (sp)
               : "x16", "x17");
#endif
  return pc;
}

// Strip {pc} of its pointer authentication code.
V8_INLINE Address StripPAC_Impl(Address pc) {
#ifdef USE_SIMULATOR
  return Simulator::StripPAC(pc, Simulator::kInstructionPointer);
#else
  asm volatile("mov x16, lr\n\t"
               "mov lr, %[pc]\n\t"
               "xpaclri\n\t"
               "mov %[pc], lr\n\t"
               "mov lr, x16\n\t"
               : [pc] "+r" (pc)
               :
               : "x16", "lr");
  return pc;
#endif
}

// clang-format on

}  // namespace pointer_auth
}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_ARM64_POINTER_AUTH_UTILS_ARM64_H_
