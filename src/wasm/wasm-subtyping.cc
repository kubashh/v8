// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-subtyping.h"

#include "src/wasm/wasm-module.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

bool IsEquivalent(ValueType subtype, ValueType supertype,
                  const WasmModule* module);

bool IsArrayTypeEquivalent(uint32_t subtype_index, uint32_t supertype_index,
                           const WasmModule* module) {
  if (module->type_kinds[subtype_index] != kWasmArrayTypeCode ||
      module->type_kinds[supertype_index] != kWasmArrayTypeCode) {
    return false;
  }

  const ArrayType* sub_array = module->types[subtype_index].array_type;
  const ArrayType* super_array = module->types[supertype_index].array_type;
  if (sub_array->mutability() != super_array->mutability()) return false;

  // Temporarily cache type equivalence for the recursive call.
  module->cache_type_equivalence(subtype_index, supertype_index);
  if (IsEquivalent(sub_array->element_type(), super_array->element_type(),
                   module)) {
    return true;
  } else {
    module->uncache_type_equivalence(subtype_index, supertype_index);
    return false;
  }
}

bool IsStructTypeEquivalent(uint32_t subtype_index, uint32_t supertype_index,
                            const WasmModule* module) {
  if (module->type_kinds[subtype_index] != kWasmStructTypeCode ||
      module->type_kinds[supertype_index] != kWasmStructTypeCode) {
    return false;
  }
  const StructType* sub_struct = module->types[subtype_index].struct_type;
  const StructType* super_struct = module->types[supertype_index].struct_type;

  if (sub_struct->field_count() != super_struct->field_count()) {
    return false;
  }

  // Temporarily cache type equivalence for the recursive call.
  module->cache_type_equivalence(subtype_index, supertype_index);
  for (uint32_t i = 0; i < sub_struct->field_count(); i++) {
    if (sub_struct->mutability(i) != super_struct->mutability(i) ||
        !IsEquivalent(sub_struct->field(i), super_struct->field(i), module)) {
      module->uncache_type_equivalence(subtype_index, supertype_index);
      return false;
    }
  }
  return true;
}

bool IsEquivalent(ValueType subtype, ValueType supertype,
                  const WasmModule* module) {
  if (subtype == supertype) return true;
  if (subtype.kind() != supertype.kind()) return false;
  if (!subtype.has_immediate()) return false;
  if (module->is_cached_subtype(subtype.ref_index(), supertype.ref_index())) {
    return true;
  } else {
    return IsArrayTypeEquivalent(subtype.ref_index(), supertype.ref_index(),
                                 module) ||
           IsStructTypeEquivalent(subtype.ref_index(), supertype.ref_index(),
                                  module);
  }
}

bool IsStructSubtype(uint32_t subtype_index, uint32_t supertype_index,
                     const WasmModule* module) {
  if (module->type_kinds[subtype_index] != kWasmStructTypeCode ||
      module->type_kinds[supertype_index] != kWasmStructTypeCode) {
    return false;
  }
  const StructType* sub_struct = module->types[subtype_index].struct_type;
  const StructType* super_struct = module->types[supertype_index].struct_type;

  if (sub_struct->field_count() < super_struct->field_count()) {
    return false;
  }

  module->cache_subtype(subtype_index, supertype_index);
  for (uint32_t i = 0; i < super_struct->field_count(); i++) {
    bool sub_mut = sub_struct->mutability(i);
    bool super_mut = super_struct->mutability(i);
    if (sub_mut != super_mut ||
        (sub_mut &&
         !IsEquivalent(sub_struct->field(i), super_struct->field(i), module)) ||
        (!sub_mut &&
         !IsSubtypeOf(sub_struct->field(i), super_struct->field(i), module))) {
      module->uncache_subtype(subtype_index, supertype_index);
      return false;
    }
  }
  return true;
}

bool IsArraySubtype(uint32_t subtype_index, uint32_t supertype_index,
                    const WasmModule* module) {
  if (module->type_kinds[subtype_index] != kWasmArrayTypeCode ||
      module->type_kinds[supertype_index] != kWasmArrayTypeCode) {
    return false;
  }
  const ArrayType* sub_array = module->types[subtype_index].array_type;
  const ArrayType* super_array = module->types[supertype_index].array_type;
  bool sub_mut = sub_array->mutability();
  bool super_mut = super_array->mutability();
  module->cache_subtype(subtype_index, supertype_index);
  if (sub_mut != super_mut ||
      (sub_mut && !IsEquivalent(sub_array->element_type(),
                                super_array->element_type(), module)) ||
      (!sub_mut && !IsSubtypeOf(sub_array->element_type(),
                                super_array->element_type(), module))) {
    module->uncache_subtype(subtype_index, supertype_index);
    return false;
  } else {
    return true;
  }
}
}  // namespace

// TODO(7748): Extend this with function subtyping.
//             Keep up to date with funcref vs. anyref subtyping.
bool IsSubtypeOfRef(ValueType subtype, ValueType supertype,
                    const WasmModule* module) {
  DCHECK(subtype != supertype && subtype.IsReferenceType() &&
         supertype.IsReferenceType());

  // eqref is a supertype of all reference types except funcref.
  if (supertype == kWasmEqRef) {
    return subtype != kWasmFuncRef;
  }

  // No other subtyping is possible except between ref and optref
  if (!((subtype.kind() == ValueType::kRef &&
         supertype.kind() == ValueType::kRef) ||
        (subtype.kind() == ValueType::kRef &&
         supertype.kind() == ValueType::kOptRef) ||
        (subtype.kind() == ValueType::kOptRef &&
         supertype.kind() == ValueType::kOptRef))) {
    return false;
  }

  if (subtype.ref_index() == supertype.ref_index()) {
    return true;
  }
  if (module->is_cached_subtype(subtype.ref_index(), supertype.ref_index())) {
    return true;
  }
  return IsStructSubtype(subtype.ref_index(), supertype.ref_index(), module) ||
         IsArraySubtype(subtype.ref_index(), supertype.ref_index(), module);
}

// TODO(7748): Extend this with function subtyping.
//             Keep up to date with funcref vs. anyref subtyping.
ValueType CommonSubType(ValueType a, ValueType b, const WasmModule* module) {
  if (a == b) return a;
  // The only sub type of any value type is {bot}.
  if (!a.IsReferenceType() || !b.IsReferenceType()) {
    return kWasmBottom;
  }
  if (IsSubtypeOf(a, b, module)) return a;
  if (IsSubtypeOf(b, a, module)) return b;
  // {a} and {b} are not each other's subtype.
  // If one of them is not nullable, their greatest subtype is bottom,
  // otherwise null.
  if (a.kind() == ValueType::kRef || b.kind() == ValueType::kRef)
    return kWasmBottom;
  return kWasmNullRef;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
