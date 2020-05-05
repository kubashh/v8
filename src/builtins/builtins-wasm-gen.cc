// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-wasm-gen.h"

#include "src/builtins/builtins-utils-gen.h"
#include "src/codegen/code-stub-assembler.h"
#include "src/codegen/interface-descriptors.h"
#include "src/objects/objects-inl.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8 {
namespace internal {

TNode<WasmInstanceObject> WasmBuiltinsAssembler::LoadInstanceFromFrame() {
  return CAST(
      LoadFromParentFrame(WasmCompiledFrameConstants::kWasmInstanceOffset));
}

TNode<NativeContext> WasmBuiltinsAssembler::LoadContextFromInstance(
    TNode<WasmInstanceObject> instance) {
  return CAST(Load(MachineType::AnyTagged(), instance,
                   IntPtrConstant(WasmInstanceObject::kNativeContextOffset -
                                  kHeapObjectTag)));
}

TNode<FixedArray> WasmBuiltinsAssembler::LoadTablesFromInstance(
    TNode<WasmInstanceObject> instance) {
  return LoadObjectField<FixedArray>(instance,
                                     WasmInstanceObject::kTablesOffset);
}

TNode<FixedArray> WasmBuiltinsAssembler::LoadExternalFunctionsFromInstance(
    TNode<WasmInstanceObject> instance) {
  return LoadObjectField<FixedArray>(
      instance, WasmInstanceObject::kWasmExternalFunctionsOffset);
}

TNode<Smi> WasmBuiltinsAssembler::SmiFromUint32WithSaturation(
    TNode<Uint32T> value, uint32_t max) {
  DCHECK_LE(max, static_cast<uint32_t>(Smi::kMaxValue));
  TNode<Uint32T> capped_value = SelectConstant(
      Uint32LessThan(value, Uint32Constant(max)), value, Uint32Constant(max));
  return SmiFromUint32(capped_value);
}

TF_BUILTIN(WasmFloat32ToNumber, WasmBuiltinsAssembler) {
  TNode<Float32T> val = UncheckedCast<Float32T>(Parameter(Descriptor::kValue));
  Return(ChangeFloat32ToTagged(val));
}

TF_BUILTIN(WasmFloat64ToNumber, WasmBuiltinsAssembler) {
  TNode<Float64T> val = UncheckedCast<Float64T>(Parameter(Descriptor::kValue));
  Return(ChangeFloat64ToTagged(val));
}

TF_BUILTIN(WasmGetOwnProperty, CodeStubAssembler) {
  TNode<Object> object = CAST(Parameter(Descriptor::kObject));
  TNode<Name> unique_name = CAST(Parameter(Descriptor::kUniqueName));
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  TVariable<Object> var_value(this);

  Label if_found(this), if_not_found(this), if_bailout(this);

  GotoIf(TaggedIsSmi(object), &if_not_found);

  GotoIf(IsUndefined(object), &if_not_found);

  TNode<Map> map = LoadMap(CAST(object));
  TNode<Uint16T> instance_type = LoadMapInstanceType(map);

  GotoIfNot(IsJSReceiverInstanceType(instance_type), &if_not_found);

  TryGetOwnProperty(context, CAST(object), CAST(object), map, instance_type,
                    unique_name, &if_found, &var_value, &if_not_found,
                    &if_bailout);

  BIND(&if_found);
  Return(var_value.value());

  BIND(&if_not_found);
  Return(UndefinedConstant());

  BIND(&if_bailout);  // This shouldn't happen when called from wasm compiler
  Unreachable();
}

// This can't be ported to Torque because it has stack parameters.
TF_BUILTIN(WasmI32AtomicWait32, WasmBuiltinsAssembler) {
  if (!Is32()) {
    Unreachable();
    return;
  }

  TNode<Uint32T> address =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kAddress));
  TNode<Number> address_number = ChangeUint32ToTagged(address);

  TNode<Int32T> expected_value =
      UncheckedCast<Int32T>(Parameter(Descriptor::kExpectedValue));
  TNode<Number> expected_value_number = ChangeInt32ToTagged(expected_value);

  TNode<IntPtrT> timeout_low =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kTimeoutLow));
  TNode<IntPtrT> timeout_high =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kTimeoutHigh));
  TNode<BigInt> timeout = BigIntFromInt32Pair(timeout_low, timeout_high);

  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);

  TNode<Smi> result_smi =
      CAST(CallRuntime(Runtime::kWasmI32AtomicWait, context, instance,
                       address_number, expected_value_number, timeout));
  Return(Unsigned(SmiToInt32(result_smi)));
}

TF_BUILTIN(WasmI64AtomicWait32, WasmBuiltinsAssembler) {
  if (!Is32()) {
    Unreachable();
    return;
  }

  TNode<Uint32T> address =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kAddress));
  TNode<Number> address_number = ChangeUint32ToTagged(address);

  TNode<IntPtrT> expected_value_low =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kExpectedValueLow));
  TNode<IntPtrT> expected_value_high =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kExpectedValueHigh));
  TNode<BigInt> expected_value =
      BigIntFromInt32Pair(expected_value_low, expected_value_high);

  TNode<IntPtrT> timeout_low =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kTimeoutLow));
  TNode<IntPtrT> timeout_high =
      UncheckedCast<IntPtrT>(Parameter(Descriptor::kTimeoutHigh));
  TNode<BigInt> timeout = BigIntFromInt32Pair(timeout_low, timeout_high);

  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);

  TNode<Smi> result_smi =
      CAST(CallRuntime(Runtime::kWasmI64AtomicWait, context, instance,
                       address_number, expected_value, timeout));
  Return(Unsigned(SmiToInt32(result_smi)));
}

}  // namespace internal
}  // namespace v8
