// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_POINTER_AUTHENTICATION_H_
#define V8_EXECUTION_POINTER_AUTHENTICATION_H_

#include "include/v8.h"
#include "src/base/macros.h"
#ifdef ENABLE_CONTROL_FLOW_INTEGRITY
#include "src/execution/arm64/pointer-auth-utils-arm64.h"
#endif

namespace v8 {
namespace internal {
namespace pointer_auth {

#ifdef ENABLE_CONTROL_FLOW_INTEGRITY
#if !defined(V8_TARGET_ARCH_ARM64)
#error "ENABLE_CONTROL_FLOW_INTEGRITY should imply V8_TARGET_ARCH_ARM64"
#endif

// Sign {pc} using {sp}. This is a macro to ensure it gets inlined.
#define POINTER_AUTH_SIGN_PC_WITH_SP(pc, sp) \
  POINTER_AUTH_SIGN_PC_WITH_SP_IMPL(pc, sp)

// Authenticate the address stored in {pc_address} and replace it with
// {new_pc}, after signing it. {offset_from_sp} is the offset between
// {pc_address} and the pointer used as a context for signing.
// We are using a macro here instead of an inline function, to avoid
// silently failing to inline this.
#define POINTER_AUTH_REPLACE_PC(pc_address, new_pc, offset_from_sp) \
  POINTER_AUTH_REPLACE_PC_IMPL(pc_address, new_pc, offset_from_sp)

// Authenticate the address stored in {pc_address} based on {old_context} and
// replace it with the same address signed with {new_context} instead.
// We are using a macro here instead of an inline function, to avoid
// silently failing to inline this.
#define POINTER_AUTH_REPLACE_CONTEXT(pc_address, old_context, new_context) \
  POINTER_AUTH_REPLACE_CONTEXT_IMPL(pc_address, old_context, new_context)

// Authenticate the address stored in {pc_address}. {offset_from_sp} is the
// offset between {pc_address} and the pointer used as a context for signing.
V8_INLINE Address AuthenticatePC(Address* pc_address, unsigned offset_from_sp) {
  return AuthenticatePC_Impl(pc_address, offset_from_sp);
}

// Strip {pc} of its pointer authentication code.
V8_INLINE Address StripPAC(Address pc) { return StripPAC_Impl(pc); }

#else

// Return {pc} without signing.
#define POINTER_AUTH_SIGN_PC_WITH_SP(pc, sp) \
  do {                                       \
    USE(pc);                                 \
    USE(sp);                                 \
  } while (false)

// Store {new_pc} to {pc_address} without signing.
#define POINTER_AUTH_REPLACE_PC(pc_address, new_pc, offset_from_sp) \
  do {                                                              \
    USE(offset_from_sp);                                            \
    *pc_address = new_pc;                                           \
  } while (false)

// Do nothing.
#define POINTER_AUTH_REPLACE_CONTEXT(pc_address, old_context, new_context) \
  do {                                                                     \
    USE(pc_address);                                                       \
    USE(old_context);                                                      \
    USE(new_context);                                                      \
  } while (false)

// Load return address from {pc_address}.
V8_INLINE Address AuthenticatePC(Address* pc_address, unsigned offset_from_sp) {
  USE(offset_from_sp);
  return *pc_address;
}

// Returns {pc} unmodified.
V8_INLINE Address StripPAC(Address pc) { return pc; }
#endif

}  // namespace pointer_auth
}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_POINTER_AUTHENTICATION_H_
