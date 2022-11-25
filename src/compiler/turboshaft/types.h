// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_TYPES_H_
#define V8_COMPILER_TURBOSHAFT_TYPES_H_

#include <cmath>
#include <limits>

#include "src/base/bit-field.h"
#include "src/base/container-utils.h"
#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/compiler/representation-change.h"
#include "src/logging/runtime-call-stats.h"
#include "src/objects/map.h"
#include "src/objects/turboshaft-types.h"
#include "src/utils/ostreams.h"
#include "src/zone/zone-containers.h"

namespace v8::internal {
class Factory;
}

namespace v8::internal::compiler::turboshaft {

namespace detail {

template <typename T>
inline bool is_unique_and_sorted(const T& container) {
  if (size(container) <= 1) return true;
  auto cur = begin(container);
  auto next = cur;
  for (++next; next != end(container); ++cur, ++next) {
    if (!(*cur < *next)) return false;
  }
  return true;
}

template <size_t Bits>
struct TypeForBits;
template <>
struct TypeForBits<32> {
  using uint_type = uint32_t;
  using float_type = float;
  static constexpr float_type nan =
      std::numeric_limits<float_type>::quiet_NaN();
};
template <>
struct TypeForBits<64> {
  using uint_type = uint64_t;
  using float_type = double;
  static constexpr float_type nan =
      std::numeric_limits<float_type>::quiet_NaN();
};

}  // namespace detail

template <size_t Bits>
using uint_type = typename detail::TypeForBits<Bits>::uint_type;
template <size_t Bits>
using float_type = typename detail::TypeForBits<Bits>::float_type;
template <size_t Bits>
constexpr float_type<Bits> nan_v = detail::TypeForBits<Bits>::nan;

template <size_t Bits>
class WordType;
template <size_t Bits>
class FloatType;

using Word32Type = WordType<32>;
using Word64Type = WordType<64>;
using Float32Type = FloatType<32>;
using Float64Type = FloatType<64>;

class V8_EXPORT_PRIVATE Type {
 public:
  enum class Kind : uint8_t {
    kInvalid,
    kNone,
    kWord32,
    kWord64,
    kFloat32,
    kFloat64,
    kAny,
  };

 protected:
  Type(Kind kind, uint8_t sub_kind, uint8_t set_size, uint32_t bitfield,
       uint64_t payload0, uint64_t payload1)
      : kind_(kind),
        sub_kind_(sub_kind),
        set_size_(set_size),
        reserved_(0),
        bitfield_(bitfield),
        payload_{payload0, payload1} {}

  explicit Type(Kind kind, uint64_t payload0 = 0, uint64_t payload1 = 0)
      : kind_(kind), bitfield_(0), payload_{payload0, payload1} {}
  Type(Kind kind, uint32_t bitfield, uint64_t payload0, uint64_t payload1)
      : kind_(kind), bitfield_(bitfield), payload_{payload0, payload1} {}

 public:
  Type() : Type(Kind::kInvalid) {}

  // Type constructors
  static inline Type Invalid() { return Type(); }
  static inline Type None() { return Type(Kind::kNone); }
  //  static inline Word32Type Word32();
  //  static inline Word32Type Word32(uint32_t constant);
  //  static inline Word32Type Word32(uint32_t from, uint32_t to);
  //  static inline Word64Type Word64();
  //  static inline Word64Type Word64(uint64_t constant);
  //  static inline Word64Type Word64(uint64_t from, uint64_t to);
  static inline Float32Type Float32(bool maybe_nan = true);
  static inline Float32Type Float32(float constant, bool maybe_nan = false);
  static inline Float32Type Float32(float range_min, float range_max,
                                    bool maybe_nan = false);
  static inline Float32Type Float32NaN();
  static inline Float64Type Float64(bool maybe_nan = true);
  static inline Float64Type Float64(double constant, bool maybe_nan = false);
  static inline Float64Type Float64(double range_min, double range_max,
                                    bool maybe_nan = false);
  static inline Float64Type Float64NaN();
  static inline Type Any() { return Type(Kind::kAny); }

  // Checks and casts
  inline Kind kind() const { return kind_; }
  inline bool IsInvalid() const { return kind_ == Kind::kInvalid; }
  inline bool IsNone() const { return kind_ == Kind::kNone; }
  inline bool IsWord32() const { return kind_ == Kind::kWord32; }
  inline bool IsWord64() const { return kind_ == Kind::kWord64; }
  inline bool IsFloat32() const { return kind_ == Kind::kFloat32; }
  inline bool IsFloat64() const { return kind_ == Kind::kFloat64; }
  inline bool IsAny() const { return kind_ == Kind::kAny; }
  template <size_t B>
  inline bool IsWord() const {
    if constexpr (B == 32)
      return IsWord32();
    else
      return IsWord64();
  }

  // Casts
  inline const Word32Type& AsWord32() const;
  inline const Word64Type& AsWord64() const;
  inline const Float32Type& AsFloat32() const;
  inline const Float64Type& AsFloat64() const;
  template <size_t B>
  inline const auto& AsWord() const {
    if constexpr (B == 32)
      return AsWord32();
    else
      return AsWord64();
  }

  // Comparison
  bool Equals(const Type& other) const;

  // Printing
  void PrintTo(std::ostream& stream) const;
  void Print() const;
  std::string ToString() const {
    std::stringstream stream;
    PrintTo(stream);
    return stream.str();
  }

  // Other functions
  Handle<TurboshaftType> AllocateOnHeap(Factory* factory) const;

 protected:
  Kind kind_;
  uint8_t sub_kind_;
  uint8_t set_size_;
  uint8_t reserved_;
  uint32_t bitfield_;
  uint64_t payload_[2];
};
static_assert(sizeof(Type) == 24);

template <size_t Bits>
class WordType : public Type {
  static_assert(Bits == 32 || Bits == 64);
  friend class Type;
  static constexpr int kMaxInlineSetSize = 2;

  enum class SubKind : uint8_t {
    kRange,
    kSet,
  };

 public:
  static constexpr int kMaxSetSize = 8;
  using word_t = uint_type<Bits>;

  // Constructors
  static WordType Complete() {
    return Range(0, std::numeric_limits<word_t>::max());
  }
  static WordType Range(word_t from, word_t to) {
    // TODO(nicohartmann@): Currently a few operations rely on the ability to
    // construct a one element range. We should consider normalizing those to
    // singleton sets here. if (from == to) return Constant(from);
    return WordType{SubKind::kRange, 0, from, to};
  }
  static WordType Set(const std::vector<word_t>& elements, Zone* zone) {
    DCHECK(detail::is_unique_and_sorted(elements));
    DCHECK_IMPLIES(elements.size() > kMaxInlineSetSize, zone != nullptr);
    DCHECK_GT(elements.size(), 0);
    DCHECK_LE(elements.size(), kMaxSetSize);

    WordType result{SubKind::kSet, static_cast<uint8_t>(elements.size()), 0L,
                    0L};
    base::Vector<word_t> v;
    if (elements.size() <= kMaxInlineSetSize) {
      // Use inline storage.
      v = base::Vector<word_t>(reinterpret_cast<word_t*>(&result.payload_[0]),
                               elements.size());
    } else {
      // Allocate storage in the zone.
      word_t* array = zone->NewArray<word_t>(elements.size());
      DCHECK(array);
      v = base::Vector<word_t>(array, elements.size());
      result.payload_[0] = base::bit_cast<uint64_t>(array);
    }
    for (size_t i = 0; i < elements.size(); ++i) v[i] = elements[i];
    return result;
  }
  static WordType Constant(word_t constant) { return Set({constant}, nullptr); }

  // Checks
  bool is_range() const { return sub_kind() == SubKind::kRange; }
  bool is_set() const { return sub_kind() == SubKind::kSet; }
  bool is_complete() const {
    return is_range() && range_to() + 1 == range_from();
  }
  bool is_constant() const {
    DCHECK_EQ(set_size_ > 0, is_set());
    return set_size_ == 1;
  }
  bool is_wrapping() const { return is_range() && range_from() > range_to(); }

  // Accessors
  word_t range_from() const {
    DCHECK(is_range());
    return static_cast<word_t>(payload_[0]);
  }
  word_t range_to() const {
    DCHECK(is_range());
    return static_cast<word_t>(payload_[1]);
  }
  std::pair<word_t, word_t> range() const {
    DCHECK(is_range());
    return {range_from(), range_to()};
  }
  int set_size() const {
    DCHECK(is_set());
    return static_cast<int>(set_size_);
  }
  word_t set_element(int index) const {
    DCHECK(is_set());
    DCHECK_GE(index, 0);
    DCHECK_LT(index, set_size());
    return set_elements()[index];
  }
  base::Vector<const word_t> set_elements() const {
    DCHECK(is_set());
    if (set_size() <= kMaxInlineSetSize) {
      return base::Vector<const word_t>(
          reinterpret_cast<const word_t*>(&payload_[0]), set_size());
    } else {
      return base::Vector<const word_t>(
          reinterpret_cast<const word_t*>(payload_[0]), set_size());
    }
  }
  base::Optional<word_t> try_get_constant() const {
    if (!is_constant()) return base::nullopt;
    DCHECK(is_set());
    DCHECK_EQ(set_size(), 1);
    return set_element(0);
  }
  word_t unsigned_min() const {
    switch (sub_kind()) {
      case SubKind::kRange:
        return is_wrapping() ? word_t{0} : range_from();
      case SubKind::kSet:
        return set_element(0);
    }
  }
  word_t unsigned_max() const {
    switch (sub_kind()) {
      case SubKind::kRange:
        return is_wrapping() ? std::numeric_limits<word_t>::max() : range_to();
      case SubKind::kSet:
        return set_element(set_size() - 1);
    }
  }

  // Misc
  bool Contains(word_t value) const;
  bool Equals(const WordType<Bits>& other) const;
  static WordType LeastUpperBound(const WordType& lhs, const WordType& rhs,
                                  Zone* zone);
  static Type Intersect(const WordType& lhs, const WordType& rhs, Zone* zone);
  void PrintTo(std::ostream& stream) const;
  Handle<TurboshaftType> AllocateOnHeap(Factory* factory) const;

 private:
  static constexpr Kind KIND = Bits == 32 ? Kind::kWord32 : Kind::kWord64;
  SubKind sub_kind() const { return static_cast<SubKind>(sub_kind_); }

  WordType(SubKind sub_kind, uint8_t set_size, uint64_t payload0 = 0,
           uint64_t payload1 = 0)
      : Type(KIND, static_cast<uint8_t>(sub_kind), set_size, 0, payload0,
             payload1) {}
};

template <size_t Bits>
class FloatType : public Type {
  static_assert(Bits == 32 || Bits == 64);
  friend class Type;
  static constexpr int kMaxInlineSetSize = 2;

  enum class SubKind : uint8_t {
    kRange,
    kSet,
    kOnlyNan,
  };

 public:
  static constexpr int kMaxSetSize = 8;
  using float_t = float_type<Bits>;

  enum Special : uint32_t {
    kNaN = 0x1,
  };

  // Constructors
  static FloatType NaN() {
    return FloatType{SubKind::kOnlyNan, 0, Special::kNaN};
  }
  static FloatType Complete(uint32_t special_values = Special::kNaN) {
    return FloatType::Range(-std::numeric_limits<float_t>::infinity(),
                            std::numeric_limits<float_t>::infinity(),
                            special_values);
  }
  static FloatType Range(float_t min, float_t max,
                         uint32_t special_values = 0) {
    DCHECK(!std::isnan(min));
    DCHECK(!std::isnan(max));
    return FloatType{
        SubKind::kRange, 0, special_values,
        static_cast<uint64_t>(base::bit_cast<uint_type<Bits>>(min)),
        static_cast<uint64_t>(base::bit_cast<uint_type<Bits>>(max))};
  }
  static FloatType Set(const std::vector<float_t>& elements,
                       uint32_t special_values, Zone* zone) {
    DCHECK(detail::is_unique_and_sorted(elements));
    DCHECK(base::none_of(elements, [](float_t f) { return std::isnan(f); }));
    DCHECK_IMPLIES(elements.size() > kMaxInlineSetSize, zone != nullptr);
    DCHECK_GT(elements.size(), 0);
    DCHECK_LE(elements.size(), kMaxSetSize);

    FloatType result{SubKind::kSet, static_cast<uint8_t>(elements.size()),
                     special_values, 0L, 0L};
    base::Vector<float_t> v;
    if (elements.size() <= kMaxInlineSetSize) {
      // Use inline storage.
      v = base::Vector<float_t>(reinterpret_cast<float_t*>(&result.payload_[0]),
                                elements.size());
    } else {
      // Allocate storage in the zone.
      float_t* array = zone->NewArray<float_t>(elements.size());
      DCHECK(array);
      v = base::Vector<float_t>(array, elements.size());
      result.payload_[0] = base::bit_cast<uint64_t>(array);
    }
    for (size_t i = 0; i < elements.size(); ++i) v[i] = elements[i];
    return result;
  }
  static FloatType Constant(float_t constant) {
    return Set({constant}, 0, nullptr);
  }

  // Checks
  bool is_only_nan() const {
    DCHECK_IMPLIES(sub_kind() == SubKind::kOnlyNan, has_nan());
    return sub_kind() == SubKind::kOnlyNan;
  }
  bool is_range() const { return sub_kind() == SubKind::kRange; }
  bool is_set() const { return sub_kind() == SubKind::kSet; }
  bool is_complete() const {
    return is_range() &&
           range_min() == -std::numeric_limits<float_t>::infinity() &&
           range_max() == std::numeric_limits<float_t>::infinity();
  }
  bool is_constant() const {
    DCHECK_EQ(set_size_ > 0, is_set());
    return set_size_ == 1 && !has_nan();
  }
  bool has_nan() const { return (bitfield_ & Special::kNaN) != 0; }

  // Accessors
  float_t range_min() const {
    DCHECK(is_range());
    return base::bit_cast<float_t>(static_cast<uint_type<Bits>>(payload_[0]));
  }
  float_t range_max() const {
    DCHECK(is_range());
    return base::bit_cast<float_t>(static_cast<uint_type<Bits>>(payload_[1]));
  }
  std::pair<float_t, float_t> range() const {
    DCHECK(is_range());
    return {range_min(), range_max()};
  }
  int set_size() const {
    DCHECK(is_set());
    return static_cast<int>(set_size_);
  }
  float_t set_element(int index) const {
    DCHECK(is_set());
    DCHECK_GE(index, 0);
    DCHECK_LT(index, set_size());
    return set_elements()[index];
  }
  base::Vector<const float_t> set_elements() const {
    DCHECK(is_set());
    if (set_size() <= kMaxInlineSetSize) {
      return base::Vector<const float_t>(
          reinterpret_cast<const float_t*>(&payload_[0]), set_size());
    } else {
      return base::Vector<const float_t>(
          reinterpret_cast<const float_t*>(payload_[0]), set_size());
    }
  }
  float_t min() const {
    switch (sub_kind()) {
      case SubKind::kOnlyNan:
        return nan_v<Bits>;
      case SubKind::kRange:
        return range_min();
      case SubKind::kSet:
        return set_element(0);
    }
  }
  float_t max() const {
    switch (sub_kind()) {
      case SubKind::kOnlyNan:
        return nan_v<Bits>;
      case SubKind::kRange:
        return range_max();
      case SubKind::kSet:
        return set_element(set_size() - 1);
    }
  }
  std::pair<float_t, float_t> minmax() const { return {min(), max()}; }
  base::Optional<float_t> try_get_constant() const {
    if (!is_constant()) return base::nullopt;
    DCHECK(is_set());
    DCHECK_EQ(set_size(), 1);
    return set_element(0);
  }

  // Misc
  bool Contains(float_t value) const;
  bool Equals(const FloatType& other) const;
  static FloatType LeastUpperBound(const FloatType& lhs, const FloatType& rhs,
                                   Zone* zone);
  static Type Intersect(const FloatType& lhs, const FloatType& rhs, Zone* zone);
  void PrintTo(std::ostream& stream) const;
  Handle<TurboshaftType> AllocateOnHeap(Factory* factory) const;

 private:
  static constexpr Kind KIND = Bits == 32 ? Kind::kFloat32 : Kind::kFloat64;
  SubKind sub_kind() const { return static_cast<SubKind>(sub_kind_); }

  FloatType(SubKind sub_kind, uint8_t set_size, uint32_t special_values,
            uint64_t payload0 = 0, uint64_t payload1 = 0)
      : Type(KIND, static_cast<uint8_t>(sub_kind), set_size, special_values,
             payload0, payload1) {
    DCHECK_EQ(special_values & ~Special::kNaN, 0);
  }
};

#if 0
// static
Word32Type Type::Word32() {
  return Word32Type{std::numeric_limits<uint32_t>::min(),
                    std::numeric_limits<uint32_t>::max()};
}
// static
Word32Type Type::Word32(uint32_t constant) {
  return Word32Type{constant, constant};
}
// static
Word32Type Type::Word32(uint32_t from, uint32_t to) {
  return Word32Type{from, to};
}
#endif

const Word32Type& Type::AsWord32() const {
  DCHECK(IsWord32());
  return *static_cast<const Word32Type*>(this);
}

#if 0
// static
Word64Type Type::Word64() {
  return Word64Type{std::numeric_limits<uint64_t>::min(),
                    std::numeric_limits<uint64_t>::max()};
}
// static
Word64Type Type::Word64(uint64_t constant) {
  return Word64Type{constant, constant};
}
// static
Word64Type Type::Word64(uint64_t from, uint64_t to) {
  return Word64Type{from, to};
}
#endif

const Word64Type& Type::AsWord64() const {
  DCHECK(IsWord64());
  return *static_cast<const Word64Type*>(this);
}

// static
Float32Type Type::Float32(bool maybe_nan /* = true */) {
  return Float32Type::Complete(maybe_nan ? Float32Type::kNaN : 0);
}
// static
Float32Type Type::Float32(float constant, bool maybe_nan /* = false */) {
  if (std::isnan(constant)) {
    return Type::Float32NaN();
  }
  return Float32Type::Set({constant}, maybe_nan ? Float32Type::kNaN : 0,
                          nullptr);
}
// static
Float32Type Type::Float32(float range_min, float range_max,
                          bool maybe_nan /* = false */) {
  DCHECK_LE(range_min, range_max);
  if (std::isnan(range_min)) {
    maybe_nan = true;
    if (std::isnan(range_max)) return Type::Float32NaN();
    range_min = -std::numeric_limits<float>::infinity();
  }
  if (std::isnan(range_max)) {
    maybe_nan = true;
    range_max = std::numeric_limits<float>::infinity();
  }
  if (std::isinf(range_min) && std::isinf(range_max)) {
    return Type::Float32(maybe_nan);
  }
  return Float32Type::Range(range_min, range_max,
                            maybe_nan ? Float32Type::kNaN : 0);
}
// static
Float32Type Type::Float32NaN() {
  return Float32Type{Float32Type::SubKind::kOnlyNan, 0, Float32Type::kNaN};
}
const Float32Type& Type::AsFloat32() const {
  DCHECK(IsFloat32());
  return *static_cast<const Float32Type*>(this);
}
// static
Float64Type Type::Float64(bool maybe_nan /* = true */) {
  return Float64Type::Complete(maybe_nan ? Float64Type::kNaN : 0);
}
// static
Float64Type Type::Float64(double constant, bool maybe_nan /* = false */) {
  if (std::isnan(constant)) {
    return Type::Float64NaN();
  }
  return Float64Type::Set({constant}, maybe_nan ? Float64Type::kNaN : 0,
                          nullptr);
}
// static
Float64Type Type::Float64(double range_min, double range_max,
                          bool maybe_nan /* = false */) {
  DCHECK_LE(range_min, range_max);
  if (std::isnan(range_min)) {
    maybe_nan = true;
    if (std::isnan(range_max)) return Type::Float64NaN();
    range_min = -std::numeric_limits<double>::infinity();
  }
  if (std::isnan(range_max)) {
    maybe_nan = true;
    range_max = std::numeric_limits<double>::infinity();
  }
  if (std::isinf(range_min) && std::isinf(range_max)) {
    return Type::Float64(maybe_nan);
  }
  return Float64Type::Range(range_min, range_max,
                            maybe_nan ? Float64Type::kNaN : 0);
}
// static
Float64Type Type::Float64NaN() {
  return Float64Type{Float64Type::SubKind::kOnlyNan, 0, Float64Type::kNaN};
}

const Float64Type& Type::AsFloat64() const {
  DCHECK(IsFloat64());
  return *static_cast<const Float64Type*>(this);
}

inline std::ostream& operator<<(std::ostream& stream, const Type& type) {
  type.PrintTo(stream);
  return stream;
}

inline bool operator==(const Type& lhs, const Type& rhs) {
  return lhs.Equals(rhs);
}

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_TYPES_H_
