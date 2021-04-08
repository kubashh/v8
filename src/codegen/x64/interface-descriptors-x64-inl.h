// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_X64_INTERFACE_DESCRIPTORS_X64_INL_H_
#define V8_CODEGEN_X64_INTERFACE_DESCRIPTORS_X64_INL_H_

#if V8_TARGET_ARCH_X64

#include "src/codegen/interface-descriptors.h"

namespace v8 {
namespace internal {

constexpr auto CallInterfaceDescriptor::DefaultRegisterArray() {
  return RegisterArray(rax, rbx, rcx, rdx, rdi);
}

constexpr Register LoadDescriptor::ReceiverRegister() { return rdx; }
constexpr Register LoadDescriptor::NameRegister() { return rcx; }
constexpr Register LoadDescriptor::SlotRegister() { return rax; }

constexpr Register LoadWithVectorDescriptor::VectorRegister() { return rbx; }

constexpr Register
LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister() {
  return rdi;
}

constexpr Register StoreDescriptor::ReceiverRegister() { return rdx; }
constexpr Register StoreDescriptor::NameRegister() { return rcx; }
constexpr Register StoreDescriptor::ValueRegister() { return rax; }
constexpr Register StoreDescriptor::SlotRegister() { return rdi; }

constexpr Register StoreWithVectorDescriptor::VectorRegister() { return rbx; }

constexpr Register StoreTransitionDescriptor::SlotRegister() { return rdi; }
constexpr Register StoreTransitionDescriptor::VectorRegister() { return rbx; }
constexpr Register StoreTransitionDescriptor::MapRegister() { return r11; }

constexpr Register ApiGetterDescriptor::HolderRegister() { return rcx; }
constexpr Register ApiGetterDescriptor::CallbackRegister() { return rbx; }

constexpr Register GrowArrayElementsDescriptor::ObjectRegister() { return rax; }
constexpr Register GrowArrayElementsDescriptor::KeyRegister() { return rbx; }

constexpr Register BaselineLeaveFrameDescriptor::ParamsSizeRegister() {
  return rbx;
}
constexpr Register BaselineLeaveFrameDescriptor::WeightRegister() {
  return rcx;
}

// static
constexpr Register TypeConversionDescriptor::ArgumentRegister() { return rax; }

// static
constexpr auto Compare_BaselineDescriptor::registers() {
  return RegisterArray(rdx, rax, rbx);
}

// static
constexpr auto BinaryOp_BaselineDescriptor::registers() {
  return RegisterArray(rdx, rax, rbx);
}

// static
constexpr auto CallTrampolineDescriptor::registers() {
  // rax : number of arguments
  // rdi : the target to call
  return RegisterArray(rdi, rax);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_X64

#endif  // V8_CODEGEN_X64_INTERFACE_DESCRIPTORS_X64_INL_H_
