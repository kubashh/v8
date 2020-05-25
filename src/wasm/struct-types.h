// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_STRUCT_TYPES_H_
#define V8_WASM_STRUCT_TYPES_H_

#include "src/base/iterator.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/wasm/value-type.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace wasm {

// A FieldType is the type of a struct field, or an array elements,
// as per the wasm-gc proposal.
// It can be mutable or immutable, and contains either a value type
// or a packed i8/i16 type.
struct FieldType {
  constexpr FieldType(ValueType value_type, bool mutability)
      : value_type(value_type), is_packed(false), mutability(mutability) {}

  constexpr FieldType(PackedType packed_type, bool mutability)
      : packed_type(packed_type), is_packed(true), mutability(mutability) {}

  union {
    ValueType value_type;
    PackedType packed_type;
  };
  bool is_packed;
  bool mutability;

  bool operator==(const FieldType& other) const {
    return mutability == other.mutability &&
           ((is_packed && other.is_packed &&
             packed_type == other.packed_type) ||
            (!is_packed && !other.is_packed && value_type == other.value_type));
  }
  bool operator!=(const FieldType& other) const {
    return mutability != other.mutability || is_packed != other.is_packed ||
           (is_packed && other.is_packed && packed_type != other.packed_type) ||
           value_type != other.value_type;
  }

#define DELEGATE(type, name, args...)                                  \
  constexpr type name(args) const {                                    \
    return is_packed ? packed_type.name(args) : value_type.name(args); \
  }

  DELEGATE(int, element_size_bytes)
  DELEGATE(MachineRepresentation, machine_representation)
  DELEGATE(char, short_name)
  DELEGATE(const char*, type_name)
#undef DELEGATE

#define DELEGATE_VALUE(name, args...) \
  constexpr bool name(args) { return !is_packed && value_type.name(args); }

  DELEGATE_VALUE(IsReferenceType)
  DELEGATE_VALUE(has_immediate)
#undef DELEGATE_VALUE

  constexpr ValueType container_type() const {
    return is_packed ? kWasmI32 : value_type;
  }
};

class StructType : public ZoneObject {
 public:
  StructType(uint32_t field_count, uint32_t* field_offsets,
             const ValueType* reps)
      : field_count_(field_count), field_offsets_(field_offsets), reps_(reps) {
    InitializeOffsets();
  }

  uint32_t field_count() const { return field_count_; }

  ValueType field(uint32_t index) const {
    DCHECK_LT(index, field_count_);
    return reps_[index];
  }

  // Iteration support.
  base::iterator_range<const ValueType*> fields() const {
    return {reps_, reps_ + field_count_};
  }

  bool operator==(const StructType& other) const {
    if (this == &other) return true;
    if (field_count() != other.field_count()) return false;
    return std::equal(fields().begin(), fields().end(), other.fields().begin());
  }
  bool operator!=(const StructType& other) const { return !(*this == other); }

  uint32_t field_offset(uint32_t index) const {
    DCHECK_LT(index, field_count());
    if (index == 0) return 0;
    return field_offsets_[index - 1];
  }
  uint32_t total_fields_size() const {
    return field_offsets_[field_count() - 1];
  }

  void InitializeOffsets() {
    uint32_t offset = field(0).element_size_bytes();
    for (uint32_t i = 1; i < field_count(); i++) {
      uint32_t field_size = field(i).element_size_bytes();
      offset = RoundUp(offset, field_size);
      field_offsets_[i - 1] = offset;
      offset += field_size;
    }
    offset = RoundUp(offset, kTaggedSize);
    field_offsets_[field_count() - 1] = offset;
  }

  // For incrementally building StructTypes.
  class Builder {
   public:
    Builder(Zone* zone, uint32_t field_count)
        : field_count_(field_count),
          zone_(zone),
          cursor_(0),
          buffer_(zone->NewArray<ValueType>(static_cast<int>(field_count))) {}

    void AddField(ValueType type) {
      DCHECK_LT(cursor_, field_count_);
      buffer_[cursor_++] = type;
    }

    StructType* Build() {
      DCHECK_EQ(cursor_, field_count_);
      uint32_t* offsets = zone_->NewArray<uint32_t>(field_count_);
      return new (zone_) StructType(field_count_, offsets, buffer_);
    }

   private:
    const uint32_t field_count_;
    Zone* zone_;
    uint32_t cursor_;
    ValueType* buffer_;
  };

 private:
  uint32_t field_count_;
  uint32_t* field_offsets_;
  const ValueType* reps_;
};

class ArrayType : public ZoneObject {
 public:
  constexpr explicit ArrayType(ValueType rep) : rep_(rep) {}

  ValueType element_type() const { return rep_; }

  bool operator==(const ArrayType& other) const { return rep_ == other.rep_; }
  bool operator!=(const ArrayType& other) const { return rep_ != other.rep_; }

 private:
  const ValueType rep_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_STRUCT_TYPES_H_
