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

TF_BUILTIN(WasmInt32ToHeapNumber, WasmBuiltinsAssembler) {
  TNode<Int32T> val = UncheckedCast<Int32T>(Parameter(Descriptor::kValue));
  Return(AllocateHeapNumberWithValue(ChangeInt32ToFloat64(val)));
}

TF_BUILTIN(WasmTaggedNonSmiToInt32, WasmBuiltinsAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  Return(
      ChangeTaggedNonSmiToInt32(context, CAST(Parameter(Descriptor::kValue))));
}

TF_BUILTIN(WasmFloat32ToNumber, WasmBuiltinsAssembler) {
  TNode<Float32T> val = UncheckedCast<Float32T>(Parameter(Descriptor::kValue));
  Return(ChangeFloat32ToTagged(val));
}

TF_BUILTIN(WasmFloat64ToNumber, WasmBuiltinsAssembler) {
  TNode<Float64T> val = UncheckedCast<Float64T>(Parameter(Descriptor::kValue));
  Return(ChangeFloat64ToTagged(val));
}

TF_BUILTIN(WasmTaggedToFloat64, WasmBuiltinsAssembler) {
  TNode<Context> context = CAST(Parameter(Descriptor::kContext));
  Return(ChangeTaggedToFloat64(context, CAST(Parameter(Descriptor::kValue))));
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

TF_BUILTIN(WasmTableCopy, WasmBuiltinsAssembler) {
  // We cap {dst}, {src}, and {size} by {wasm::kV8MaxWasmTableSize + 1} to make
  // sure that the values fit into a Smi.
  STATIC_ASSERT(static_cast<size_t>(Smi::kMaxValue) >=
                wasm::kV8MaxWasmTableSize + 1);
  constexpr uint32_t kCap =
      static_cast<uint32_t>(wasm::kV8MaxWasmTableSize + 1);

  TNode<Uint32T> dst_raw =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kDestination));
  TNode<Smi> dst = SmiFromUint32WithSaturation(dst_raw, kCap);

  TNode<Uint32T> src_raw =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kSource));
  TNode<Smi> src = SmiFromUint32WithSaturation(src_raw, kCap);

  TNode<Uint32T> size_raw =
      UncheckedCast<Uint32T>(Parameter(Descriptor::kSize));
  TNode<Smi> size = SmiFromUint32WithSaturation(size_raw, kCap);

  TNode<Smi> dst_table =
      UncheckedCast<Smi>(Parameter(Descriptor::kDestinationTable));

  TNode<Smi> src_table =
      UncheckedCast<Smi>(Parameter(Descriptor::kSourceTable));

  TNode<WasmInstanceObject> instance = LoadInstanceFromFrame();
  TNode<Context> context = LoadContextFromInstance(instance);

  TailCallRuntime(Runtime::kWasmTableCopy, context, instance, dst_table,
                  src_table, dst, src, size);
}

}  // namespace internal
}  // namespace v8
