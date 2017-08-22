// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_MIPS

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const AsmRegister CallInterfaceDescriptor::ContextRegister() { return cp; }

void CallInterfaceDescriptor::DefaultInitializePlatformSpecific(
    CallInterfaceDescriptorData* data, int register_parameter_count) {
  const AsmRegister default_stub_registers[] = {a0, a1, a2, a3, t0};
  CHECK_LE(static_cast<size_t>(register_parameter_count),
           arraysize(default_stub_registers));
  data->InitializePlatformSpecific(register_parameter_count,
                                   default_stub_registers);
}

const AsmRegister FastNewFunctionContextDescriptor::FunctionRegister() {
  return a1;
}
const AsmRegister FastNewFunctionContextDescriptor::SlotsRegister() {
  return a0;
}

const AsmRegister LoadDescriptor::ReceiverRegister() { return a1; }
const AsmRegister LoadDescriptor::NameRegister() { return a2; }
const AsmRegister LoadDescriptor::SlotRegister() { return a0; }

const AsmRegister LoadWithVectorDescriptor::VectorRegister() { return a3; }

const AsmRegister LoadICProtoArrayDescriptor::HandlerRegister() { return t0; }

const AsmRegister StoreDescriptor::ReceiverRegister() { return a1; }
const AsmRegister StoreDescriptor::NameRegister() { return a2; }
const AsmRegister StoreDescriptor::ValueRegister() { return a0; }
const AsmRegister StoreDescriptor::SlotRegister() { return t0; }

const AsmRegister StoreWithVectorDescriptor::VectorRegister() { return a3; }

const AsmRegister StoreTransitionDescriptor::SlotRegister() { return t0; }
const AsmRegister StoreTransitionDescriptor::VectorRegister() { return a3; }
const AsmRegister StoreTransitionDescriptor::MapRegister() { return t1; }

const AsmRegister StringCompareDescriptor::LeftRegister() { return a1; }
const AsmRegister StringCompareDescriptor::RightRegister() { return a0; }

const AsmRegister ApiGetterDescriptor::HolderRegister() { return a0; }
const AsmRegister ApiGetterDescriptor::CallbackRegister() { return a3; }

const AsmRegister MathPowTaggedDescriptor::exponent() { return a2; }

const AsmRegister MathPowIntegerDescriptor::exponent() {
  return MathPowTaggedDescriptor::exponent();
}

const AsmRegister GrowArrayElementsDescriptor::ObjectRegister() { return a0; }
const AsmRegister GrowArrayElementsDescriptor::KeyRegister() { return a3; }

void FastNewClosureDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {a1, a2, a3};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}

// static
const AsmRegister TypeConversionDescriptor::ArgumentRegister() { return a0; }

void TypeofDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {a3};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}


void FastCloneRegExpDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {a3, a2, a1, a0};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void FastCloneShallowArrayDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {a3, a2, a1};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void FastCloneShallowObjectDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {a3, a2, a1, a0};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}

void CallFunctionDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {a1};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}

void CallTrampolineDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // a1: target
  // a0: number of arguments
  AsmRegister registers[] = {a1, a0};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void CallVarargsDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // a0 : number of arguments (on the stack, not including receiver)
  // a1 : the target to call
  // a2 : arguments list (FixedArray)
  // t0 : arguments list length (untagged)
  AsmRegister registers[] = {a1, a0, a2, t0};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void CallForwardVarargsDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // a1: the target to call
  // a0: number of arguments
  // a2: start index (to support rest parameters)
  AsmRegister registers[] = {a1, a0, a2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void CallWithSpreadDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // a0 : number of arguments (on the stack, not including receiver)
  // a1 : the target to call
  // a2 : the object to spread
  AsmRegister registers[] = {a1, a0, a2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void CallWithArrayLikeDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // a1 : the target to call
  // a2 : the arguments list
  AsmRegister registers[] = {a1, a2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ConstructVarargsDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // a0 : number of arguments (on the stack, not including receiver)
  // a1 : the target to call
  // a3 : the new target
  // a2 : arguments list (FixedArray)
  // t0 : arguments list length (untagged)
  AsmRegister registers[] = {a1, a3, a0, a2, t0};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ConstructForwardVarargsDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // a1: the target to call
  // a3: new target
  // a0: number of arguments
  // a2: start index (to support rest parameters)
  AsmRegister registers[] = {a1, a3, a0, a2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ConstructWithSpreadDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // a0 : number of arguments (on the stack, not including receiver)
  // a1 : the target to call
  // a3 : the new target
  // a2 : the object to spread
  AsmRegister registers[] = {a1, a3, a0, a2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ConstructWithArrayLikeDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // a1 : the target to call
  // a3 : the new target
  // a2 : the arguments list
  AsmRegister registers[] = {a1, a3, a2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ConstructStubDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // a1: target
  // a3: new target
  // a0: number of arguments
  // a2: allocation site or undefined
  AsmRegister registers[] = {a1, a3, a0, a2};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void ConstructTrampolineDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // a1: target
  // a3: new target
  // a0: number of arguments
  AsmRegister registers[] = {a1, a3, a0};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void TransitionElementsKindDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {a0, a1};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}


void AllocateHeapNumberDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // register state
  data->InitializePlatformSpecific(0, nullptr, nullptr);
}

void ArrayConstructorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // kTarget, kNewTarget, kActualArgumentsCount, kAllocationSite
  AsmRegister registers[] = {a1, a3, a0, a2};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}

void ArrayNoArgumentConstructorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // register state
  // a0 -- number of arguments
  // a1 -- function
  // a2 -- allocation site with elements kind
  AsmRegister registers[] = {a1, a2, a0};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}

void ArraySingleArgumentConstructorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // register state
  // a0 -- number of arguments
  // a1 -- function
  // a2 -- allocation site with elements kind
  AsmRegister registers[] = {a1, a2, a0};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}

void ArrayNArgumentsConstructorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  // stack param count needs (constructor pointer, and single argument)
  AsmRegister registers[] = {a1, a2, a0};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void CompareDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {a1, a0};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}


void BinaryOpDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {a1, a0};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}

void StringAddDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {a1, a0};
  data->InitializePlatformSpecific(arraysize(registers), registers, NULL);
}

void ArgumentAdaptorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      a1,  // JSFunction
      a3,  // the new target
      a0,  // actual number of arguments
      a2,  // expected number of arguments
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ApiCallbackDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      a0,  // callee
      t0,  // call_data
      a2,  // holder
      a1,  // api_function_address
  };
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
      a0,  // argument count (not including receiver)
      a2,  // address of first argument
      a1   // the target callable to be call
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void InterpreterPushArgsThenConstructDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      a0,  // argument count (not including receiver)
      a3,  // new target
      a1,  // constructor to call
      a2,  // allocation site feedback if available, undefined otherwise.
      t4   // address of the first argument
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void InterpreterCEntryDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      a0,  // argument count (argc)
      a2,  // address of first argument (argv)
      a1   // the runtime function to call
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ResumeGeneratorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      v0,  // the value to pass to the generator
      a1,  // the JSGeneratorObject to resume
      a2   // the resume mode (tagged)
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void FrameDropperTrampolineDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  AsmRegister registers[] = {
      a1,  // loaded new FP
  };
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_MIPS
