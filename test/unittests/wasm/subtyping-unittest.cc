// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-subtyping.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace subtyping_unittest {

class WasmSubtypingTest : public TestWithZone {};
using FieldInit = std::pair<ValueType, bool>;

ValueType wasmRef(uint32_t index) { return ValueType(ValueType::kRef, index); }
ValueType wasmOptRef(uint32_t index) {
  return ValueType(ValueType::kOptRef, index);
}

FieldInit mut(ValueType type) { return FieldInit(type, true); }
FieldInit immut(ValueType type) { return FieldInit(type, false); }

void DefineStruct(WasmModule* module, std::initializer_list<FieldInit> fields) {
  StructType::Builder builder(module->signature_zone.get(),
                              static_cast<uint32_t>(fields.size()));
  for (FieldInit field : fields) {
    builder.AddField(field.first, field.second);
  }
  return module->add_struct_type(builder.Build());
}

void DefineArray(WasmModule* module, FieldInit element_type) {
  module->add_array_type(new (module->signature_zone.get()) ArrayType(
      element_type.first, element_type.second));
}

TEST_F(WasmSubtypingTest, Subtyping) {
  v8::internal::AccountingAllocator allocator;

  WasmModule module_(std::make_unique<Zone>(*(zone())));

  WasmModule* module = &module_;

  /* 0 */ DefineStruct(module, {mut(wasmRef(2)), immut(wasmOptRef(2))});
  /* 1 */ DefineStruct(module, {mut(wasmRef(2)), immut(wasmRef(2))});
  /* 2 */ DefineArray(module, immut(wasmRef(0)));
  /* 3 */ DefineArray(module, immut(wasmRef(1)));
  /* 4 */ DefineStruct(module,
                       {mut(wasmRef(2)), immut(wasmRef(3)), immut(kWasmF64)});
  /* 5 */ DefineStruct(module, {mut(wasmOptRef(2)), immut(wasmRef(2))});
  /* 6 */ DefineArray(module, mut(kWasmI32));
  /* 7 */ DefineArray(module, immut(kWasmI32));
  /* 8 */ DefineStruct(module, {mut(kWasmI32), immut(wasmOptRef(8))});
  /* 9 */ DefineStruct(module, {mut(kWasmI32), immut(wasmOptRef(8))});

  ValueType value_types[] = {kWasmI32, kWasmI64, kWasmF32, kWasmF64};
  ValueType ref_types[] = {kWasmAnyRef,   kWasmFuncRef,  kWasmExnRef,
                           kWasmEqRef,    wasmOptRef(0), wasmRef(0),
                           wasmOptRef(2), wasmRef(2)};

  // Value types are unrelated, except if they are equal.
  for (ValueType subtype : value_types) {
    for (ValueType supertype : value_types) {
      CHECK_EQ(IsSubtypeOf(subtype, supertype, module), subtype == supertype);
    }
  }

  // Value types are unrelated with reference types.
  for (ValueType value_type : value_types) {
    for (ValueType ref_type : ref_types) {
      CHECK(!IsSubtypeOf(value_type, ref_type, module));
      CHECK(!IsSubtypeOf(ref_type, value_type, module));
    }
  }

  for (ValueType ref_type : ref_types) {
    // Reference types are a subtype of eqref, except funcref.
    CHECK_EQ(IsSubtypeOf(ref_type, kWasmEqRef, module),
             ref_type != kWasmFuncRef);
    // Each reference type is a subtype of itself.
    CHECK(IsSubtypeOf(ref_type, ref_type, module));
  }

  for (ValueType type_1 : {kWasmAnyRef, kWasmFuncRef, kWasmExnRef}) {
    for (ValueType type_2 : {kWasmAnyRef, kWasmFuncRef, kWasmExnRef}) {
      CHECK_EQ(IsSubtypeOf(type_1, type_2, module), type_1 == type_2);
    }
  }

  // Unrelated refs are unrelated.
  CHECK(!IsSubtypeOf(wasmRef(0), wasmRef(2), module));
  CHECK(!IsSubtypeOf(wasmOptRef(3), wasmOptRef(1), module));
  // ref is a subtype of optref for the same struct/array.
  CHECK(IsSubtypeOf(wasmRef(0), wasmOptRef(0), module));
  CHECK(IsSubtypeOf(wasmRef(2), wasmOptRef(2), module));
  // optref is not a subtype of ref for the same struct/array.
  CHECK(!IsSubtypeOf(wasmOptRef(0), wasmRef(0), module));
  CHECK(!IsSubtypeOf(wasmOptRef(2), wasmRef(2), module));
  // Prefix subtyping for structs.
  CHECK(IsSubtypeOf(wasmOptRef(4), wasmOptRef(0), module));
  // Mutable fields are invariant.
  CHECK(!IsSubtypeOf(wasmRef(0), wasmRef(5), module));
  // Immutable fields are covariant.
  CHECK(IsSubtypeOf(wasmRef(1), wasmRef(0), module));
  // Prefix subtyping + immutable field covariance for structs.
  CHECK(IsSubtypeOf(wasmOptRef(4), wasmOptRef(1), module));
  // ref is a subtype of optref if the same is true for the underlying
  // structs/arrays.
  CHECK(IsSubtypeOf(wasmRef(3), wasmOptRef(2), module));
  // No subtyping between mutable/immutable fields.
  CHECK(!IsSubtypeOf(wasmRef(7), wasmRef(6), module));
  CHECK(!IsSubtypeOf(wasmRef(6), wasmRef(7), module));
  // Recursive types
  CHECK(IsSubtypeOf(wasmRef(9), wasmRef(8), module));
}

}  // namespace subtyping_unittest
}  // namespace wasm
}  // namespace internal
}  // namespace v8
