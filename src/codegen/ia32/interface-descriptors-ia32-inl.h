// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_IA32_INTERFACE_DESCRIPTORS_IA32_INL_H_
#define V8_CODEGEN_IA32_INTERFACE_DESCRIPTORS_IA32_INL_H_

#if V8_TARGET_ARCH_IA32

#include "src/codegen/interface-descriptors.h"

namespace v8 {
namespace internal {

constexpr auto CallInterfaceDescriptor::DefaultRegisterArray() {
  auto registers = RegisterArray(eax, ecx, edx, edi);
  STATIC_ASSERT(registers.size() == kMaxBuiltinRegisterParams);
  return registers;
}

// static
constexpr auto RecordWriteDescriptor::registers() {
  return RegisterArray(ecx, edx, esi, edi, kReturnRegister0);
}

// static
constexpr auto DynamicCheckMapsDescriptor::registers() {
  return RegisterArray(eax, ecx, edx, edi, esi);
}

// static
constexpr auto EphemeronKeyBarrierDescriptor::registers() {
  return RegisterArray(ecx, edx, esi, edi, kReturnRegister0);
}

constexpr Register LoadDescriptor::ReceiverRegister() { return edx; }
constexpr Register LoadDescriptor::NameRegister() { return ecx; }
constexpr Register LoadDescriptor::SlotRegister() { return eax; }

constexpr Register LoadWithVectorDescriptor::VectorRegister() { return no_reg; }

constexpr Register
LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister() {
  return edi;
}

constexpr Register StoreDescriptor::ReceiverRegister() { return edx; }
constexpr Register StoreDescriptor::NameRegister() { return ecx; }
constexpr Register StoreDescriptor::ValueRegister() { return no_reg; }
constexpr Register StoreDescriptor::SlotRegister() { return no_reg; }

constexpr Register StoreWithVectorDescriptor::VectorRegister() {
  return no_reg;
}

constexpr Register StoreTransitionDescriptor::MapRegister() { return edi; }

constexpr Register ApiGetterDescriptor::HolderRegister() { return ecx; }
constexpr Register ApiGetterDescriptor::CallbackRegister() { return eax; }

constexpr Register GrowArrayElementsDescriptor::ObjectRegister() { return eax; }
constexpr Register GrowArrayElementsDescriptor::KeyRegister() { return ecx; }

constexpr Register BaselineLeaveFrameDescriptor::ParamsSizeRegister() {
  return esi;
}
constexpr Register BaselineLeaveFrameDescriptor::WeightRegister() {
  return edi;
}

constexpr Register TypeConversionDescriptor::ArgumentRegister() { return eax; }

// staic
constexpr auto TypeofDescriptor::registers() { return RegisterArray(ecx); }

// staic
constexpr auto CallTrampolineDescriptor::registers() {
  // eax : number of arguments
  // edi : the target to call
  return RegisterArray(edi, eax);
}

// staic
constexpr auto CallVarargsDescriptor::registers() {
  // eax : number of arguments (on the stack, not including receiver)
  // edi : the target to call
  // ecx : arguments list length (untagged)
  // On the stack : arguments list (FixedArray)
  return RegisterArray(edi, eax, ecx);
}

// staic
constexpr auto CallForwardVarargsDescriptor::registers() {
  // eax : number of arguments
  // ecx : start index (to support rest parameters)
  // edi : the target to call
  return RegisterArray(edi, eax, ecx);
}

// staic
constexpr auto CallFunctionTemplateDescriptor::registers() {
  // edx : function template info
  // ecx : number of arguments (on the stack, not including receiver)
  return RegisterArray(edx, ecx);
}

// staic
constexpr auto CallWithSpreadDescriptor::registers() {
  // eax : number of arguments (on the stack, not including receiver)
  // edi : the target to call
  // ecx : the object to spread
  return RegisterArray(edi, eax, ecx);
}

// staic
constexpr auto CallWithArrayLikeDescriptor::registers() {
  // edi : the target to call
  // edx : the arguments list
  return RegisterArray(edi, edx);
}

// staic
constexpr auto ConstructVarargsDescriptor::registers() {
  // eax : number of arguments (on the stack, not including receiver)
  // edi : the target to call
  // edx : the new target
  // ecx : arguments list length (untagged)
  // On the stack : arguments list (FixedArray)
  return RegisterArray(edi, edx, eax, ecx);
}

// staic
constexpr auto ConstructForwardVarargsDescriptor::registers() {
  // eax : number of arguments
  // edx : the new target
  // ecx : start index (to support rest parameters)
  // edi : the target to call
  return RegisterArray(edi, edx, eax, ecx);
}

// staic
constexpr auto ConstructWithSpreadDescriptor::registers() {
  // eax : number of arguments (on the stack, not including receiver)
  // edi : the target to call
  // edx : the new target
  // ecx : the object to spread
  return RegisterArray(edi, edx, eax, ecx);
}

// staic
constexpr auto ConstructWithArrayLikeDescriptor::registers() {
  // edi : the target to call
  // edx : the new target
  // ecx : the arguments list
  return RegisterArray(edi, edx, ecx);
}

// staic
constexpr auto ConstructStubDescriptor::registers() {
  // eax : number of arguments
  // edx : the new target
  // edi : the target to call
  // ecx : allocation site or undefined
  // TODO(jgruber): Remove the unused allocation site parameter.
  return RegisterArray(edi, edx, eax, ecx);
}

// staic
constexpr auto AbortDescriptor::registers() { return RegisterArray(edx); }

// staic
constexpr auto CompareDescriptor::registers() {
  return RegisterArray(edx, eax);
}

// staic
constexpr auto Compare_BaselineDescriptor::registers() {
  return RegisterArray(edx, eax, ecx);
}

// staic
constexpr auto BinaryOpDescriptor::registers() {
  return RegisterArray(edx, eax);
}

// staic
constexpr auto BinaryOp_BaselineDescriptor::registers() {
  return RegisterArray(edx, eax, ecx);
}

// staic
constexpr auto ApiCallbackDescriptor::registers() {
  return RegisterArray(edx,   // kApiFunctionAddress
                       ecx,   // kArgc
                       eax,   // kCallData
                       edi);  // kHolder
}

// staic
constexpr auto InterpreterDispatchDescriptor::registers() {
  return RegisterArray(
      kInterpreterAccumulatorRegister, kInterpreterBytecodeOffsetRegister,
      kInterpreterBytecodeArrayRegister, kInterpreterDispatchTableRegister);
}

// staic
constexpr auto InterpreterPushArgsThenCallDescriptor::registers() {
  return RegisterArray(eax,   // argument count (not including receiver)
                       ecx,   // address of first argument
                       edi);  // the target callable to be call
}

// staic
constexpr auto InterpreterPushArgsThenConstructDescriptor::registers() {
  return RegisterArray(eax,   // argument count (not including receiver)
                       ecx);  // address of first argument
}

// staic
constexpr auto ResumeGeneratorDescriptor::registers() {
  return RegisterArray(eax,   // the value to pass to the generator
                       edx);  // the JSGeneratorObject to resume
}

// staic
constexpr auto FrameDropperTrampolineDescriptor::registers() {
  return RegisterArray(eax);  // loaded new FP
}

// static
constexpr auto RunMicrotasksEntryDescriptor::registers() {
  return RegisterArray();
}

// staic
constexpr auto WasmFloat32ToNumberDescriptor::registers() {
  // Work around using eax, whose register code is 0, and leads to the FP
  // parameter being passed via xmm0, which is not allocatable on ia32.
  return RegisterArray(ecx);
}

// staic
constexpr auto WasmFloat64ToNumberDescriptor::registers() {
  // Work around using eax, whose register code is 0, and leads to the FP
  // parameter being passed via xmm0, which is not allocatable on ia32.
  return RegisterArray(ecx);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_IA32

#endif  // V8_CODEGEN_IA32_INTERFACE_DESCRIPTORS_IA32_INL_H_
