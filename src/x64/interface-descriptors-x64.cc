// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_X64

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const AsmRegister CallInterfaceDescriptor::ContextRegister() { return rsi; }

void CallInterfaceDescriptor::DefaultInitializePlatformSpecific(
    CallInterfaceDescriptorData* data, int register_parameter_count) {
  const AsmRegister default_stub_registers[] = {rax, rbx, rcx, rdx, rdi};
  CHECK_LE(static_cast<size_t>(register_parameter_count),
           arraysize(default_stub_registers));
  data->InitializePlatformSpecific(register_parameter_count,
                                   default_stub_registers);
}

void RecordWriteDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  const AsmRegister default_stub_registers[] = {arg_reg_1, arg_reg_2, arg_reg_3,
                                                arg_reg_4, kReturnRegister0};

  data->RestrictAllocatableRegisters(default_stub_registers,
                                     arraysize(default_stub_registers));

  CHECK_LE(static_cast<size_t>(kParameterCount),
           arraysize(default_stub_registers));
  data->InitializePlatformSpecific(kParameterCount, default_stub_registers);
}

const AsmRegister FastNewFunctionContextDescriptor::FunctionRegister() {
  return rdi;
}
const AsmRegister FastNewFunctionContextDescriptor::SlotsRegister() {
  return rax;
}

const AsmRegister LoadDescriptor::ReceiverRegister() { return rdx; }
const AsmRegister LoadDescriptor::NameRegister() { return rcx; }
const AsmRegister LoadDescriptor::SlotRegister() { return rax; }

const AsmRegister LoadWithVectorDescriptor::VectorRegister() { return rbx; }

const AsmRegister LoadICProtoArrayDescriptor::HandlerRegister() { return rdi; }

const AsmRegister StoreDescriptor::ReceiverRegister() { return rdx; }
const AsmRegister StoreDescriptor::NameRegister() { return rcx; }
const AsmRegister StoreDescriptor::ValueRegister() { return rax; }
const AsmRegister StoreDescriptor::SlotRegister() { return rdi; }

const AsmRegister StoreWithVectorDescriptor::VectorRegister() { return rbx; }

const AsmRegister StoreTransitionDescriptor::SlotRegister() { return rdi; }
const AsmRegister StoreTransitionDescriptor::VectorRegister() { return rbx; }
const AsmRegister StoreTransitionDescriptor::MapRegister() { return r11; }

const AsmRegister StringCompareDescriptor::LeftRegister() { return rdx; }
const AsmRegister StringCompareDescriptor::RightRegister() { return rax; }

const AsmRegister ApiGetterDescriptor::HolderRegister() { return rcx; }
const AsmRegister ApiGetterDescriptor::CallbackRegister() { return rbx; }

const AsmRegister MathPowTaggedDescriptor::exponent() { return rdx; }

const AsmRegister MathPowIntegerDescriptor::exponent() {
  return MathPowTaggedDescriptor::exponent();
}

const AsmRegister GrowArrayElementsDescriptor::ObjectRegister() { return rax; }
const AsmRegister GrowArrayElementsDescriptor::KeyRegister() { return rbx; }

void FastNewClosureDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // SharedFunctionInfo, vector, slot index.
  AsmRegister registers[] = {rbx, rcx, rdx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void TypeofDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {rbx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


// static
const AsmRegister TypeConversionDescriptor::ArgumentRegister() { return rax; }

void FastCloneRegExpDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {rdi, rax, rcx, rdx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void FastCloneShallowArrayDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {rax, rbx, rcx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void FastCloneShallowObjectDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {rax, rbx, rcx, rdx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void CallFunctionDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {rdi};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void CallTrampolineDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // rax : number of arguments
  // rdi : the target to call
  AsmRegister registers[] = {rdi, rax};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void CallVarargsDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // rax : number of arguments (on the stack, not including receiver)
  // rdi : the target to call
  // rbx : arguments list (FixedArray)
  // rcx : arguments list length (untagged)
  AsmRegister registers[] = {rdi, rax, rbx, rcx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void CallForwardVarargsDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // rax : number of arguments
  // rcx : start index (to support rest parameters)
  // rdi : the target to call
  AsmRegister registers[] = {rdi, rax, rcx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void CallWithSpreadDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // rax : number of arguments (on the stack, not including receiver)
  // rdi : the target to call
  // rbx : the object to spread
  AsmRegister registers[] = {rdi, rax, rbx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void CallWithArrayLikeDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // rdi : the target to call
  // rbx : the arguments list
  AsmRegister registers[] = {rdi, rbx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ConstructVarargsDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // rax : number of arguments (on the stack, not including receiver)
  // rdi : the target to call
  // rdx : the new target
  // rbx : arguments list (FixedArray)
  // rcx : arguments list length (untagged)
  AsmRegister registers[] = {rdi, rdx, rax, rbx, rcx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ConstructForwardVarargsDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // rax : number of arguments
  // rdx : the new target
  // rcx : start index (to support rest parameters)
  // rdi : the target to call
  AsmRegister registers[] = {rdi, rdx, rax, rcx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ConstructWithSpreadDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // rax : number of arguments (on the stack, not including receiver)
  // rdi : the target to call
  // rdx : the new target
  // rbx : the object to spread
  AsmRegister registers[] = {rdi, rdx, rax, rbx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ConstructWithArrayLikeDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // rdi : the target to call
  // rdx : the new target
  // rbx : the arguments list
  AsmRegister registers[] = {rdi, rdx, rbx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ConstructStubDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // rax : number of arguments
  // rdx : the new target
  // rdi : the target to call
  // rbx : allocation site or undefined
  AsmRegister registers[] = {rdi, rdx, rax, rbx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void ConstructTrampolineDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // rax : number of arguments
  // rdx : the new target
  // rdi : the target to call
  AsmRegister registers[] = {rdi, rdx, rax};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void TransitionElementsKindDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {rax, rbx};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void AllocateHeapNumberDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  data->InitializePlatformSpecific(0, nullptr, nullptr);
}

void ArrayConstructorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // kTarget, kNewTarget, kActualArgumentsCount, kAllocationSite
  AsmRegister registers[] = {rdi, rdx, rax, rbx};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}

void ArrayNoArgumentConstructorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // register state
  // rax -- number of arguments
  // rdi -- function
  // rbx -- allocation site with elements kind
  AsmRegister registers[] = {rdi, rbx, rax};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}

void ArraySingleArgumentConstructorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // register state
  // rax -- number of arguments
  // rdi -- function
  // rbx -- allocation site with elements kind
  AsmRegister registers[] = {rdi, rbx, rax};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}

void ArrayNArgumentsConstructorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // register state
  // rax -- number of arguments
  // rdi -- function
  // rbx -- allocation site with elements kind
  AsmRegister registers[] = {rdi, rbx, rax};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}

void CompareDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {rdx, rax};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void BinaryOpDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {rdx, rax};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void StringAddDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {rdx, rax};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ArgumentAdaptorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      rdi,  // JSFunction
      rdx,  // the new target
      rax,  // actual number of arguments
      rbx,  // expected number of arguments
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ApiCallbackDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      rdi,  // callee
      rbx,  // call_data
      rcx,  // holder
      rdx,  // api_function_address
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void InterpreterExitTrampolineDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {kInterpreterAccumulatorRegister};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void InterpreterDispatchDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      kInterpreterAccumulatorRegister, kInterpreterBytecodeOffsetRegister,
      kInterpreterBytecodeArrayRegister, kInterpreterDispatchTableRegister};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void InterpreterPushArgsThenCallDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      rax,  // argument count (not including receiver)
      rbx,  // address of first argument
      rdi   // the target callable to be call
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void InterpreterPushArgsThenConstructDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      rax,  // argument count (not including receiver)
      rdx,  // new target
      rdi,  // constructor
      rbx,  // allocation site feedback if available, undefined otherwise
      rcx,  // address of first argument
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void InterpreterCEntryDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      rax,  // argument count (argc)
      r15,  // address of first argument (argv)
      rbx   // the runtime function to call
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ResumeGeneratorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      rax,  // the value to pass to the generator
      rbx,  // the JSGeneratorObject / JSAsyncGeneratorObject to resume
      rdx   // the resume mode (tagged)
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void FrameDropperTrampolineDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      rbx,  // loaded new FP
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_X64
