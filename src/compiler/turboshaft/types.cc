// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/types.h"

#include <sstream>

#include "src/base/logging.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/representation-change.h"
#include "src/heap/factory.h"
#include "src/objects/turboshaft-types-inl.h"

namespace v8::internal::compiler::turboshaft {

namespace {

std::pair<uint32_t, uint32_t> uint64_to_high_low(uint64_t value) {
  return {static_cast<uint32_t>(value >> 32), static_cast<uint32_t>(value)};
}

template <size_t Bits,
          typename T = std::conditional_t<Bits == 32, float, double>>
bool float_equal(T lhs, T rhs) {
  // TODO(nicohartmann@): Replace by something proper.
  return lhs == rhs;
}

}  // namespace

bool Type::Equals(const Type& other) const {
  DCHECK(!IsInvalid());
  DCHECK(!other.IsInvalid());

  if (kind_ != other.kind_) return false;
  switch (kind_) {
    case Kind::kInvalid:
      UNREACHABLE();
    case Kind::kNone:
      return true;
    case Kind::kWord32:
      return AsWord32().Equals(other.AsWord32());
    case Kind::kWord64:
      return AsWord64().Equals(other.AsWord64());
    case Kind::kFloat32:
      return AsFloat32().Equals(other.AsFloat32());
    case Kind::kFloat64:
      return AsFloat64().Equals(other.AsFloat64());
    case Kind::kAny:
      return true;
  }
}

void Type::PrintTo(std::ostream& stream) const {
  switch (kind_) {
    case Kind::kInvalid:
      UNREACHABLE();
    case Kind::kNone:
      stream << "None";
      break;
    case Kind::kWord32: {
      AsWord32().PrintTo(stream);
      break;
    }
    case Kind::kWord64: {
      AsWord64().PrintTo(stream);
      break;
    }
    case Kind::kFloat32: {
      AsFloat32().PrintTo(stream);
      break;
    }
    case Kind::kFloat64: {
      AsFloat64().PrintTo(stream);
      break;
    }
    case Kind::kAny: {
      stream << "Any";
      break;
    }
  }
}

void Type::Print() const {
  StdoutStream os;
  PrintTo(os);
  os << std::endl;
}

Handle<TurboshaftType> Type::AllocateOnHeap(Factory* factory) const {
  DCHECK_NOT_NULL(factory);
  switch (kind_) {
    case Kind::kInvalid:
      UNREACHABLE();
    case Kind::kNone:
      UNIMPLEMENTED();
    case Kind::kWord32:
      return AsWord32().AllocateOnHeap(factory);
    case Kind::kWord64:
      return AsWord64().AllocateOnHeap(factory);
    case Kind::kFloat32:
      return AsFloat32().AllocateOnHeap(factory);
    case Kind::kFloat64:
      return AsFloat64().AllocateOnHeap(factory);
    case Kind::kAny:
      UNIMPLEMENTED();
  }
}

template <size_t Bits>
bool WordType<Bits>::Contains(word_t value) const {
  switch (sub_kind()) {
    case SubKind::kRange: {
      if (is_wrapping()) return range_to() >= value || range_from() <= value;
      return range_from() <= value && value <= range_to();
    }
    case SubKind::kSet: {
      for (int i = 0; i < set_size(); ++i) {
        if (set_element(i) == value) return true;
      }
      return false;
    }
  }
}

template <size_t Bits>
bool WordType<Bits>::Equals(const WordType<Bits>& other) const {
  if (sub_kind() != other.sub_kind()) return false;
  switch (sub_kind()) {
    case SubKind::kRange:
      return (range_from() == other.range_from() &&
              range_to() == other.range_to()) ||
             (is_complete() && other.is_complete());
    case SubKind::kSet: {
      if (set_size() != other.set_size()) return false;
      for (int i = 0; i < set_size(); ++i) {
        if (set_element(i) != other.set_element(i)) return false;
      }
      return true;
    }
  }
}

template <size_t Bits>
// static
WordType<Bits> WordType<Bits>::LeastUpperBound(const WordType<Bits>& lhs,
                                               const WordType<Bits>& rhs,
                                               Zone* zone) {
  if (lhs.is_set()) {
    if (!rhs.is_set()) {
      if (lhs.set_size() == 1) {
        word_t e = lhs.set_element(0);
        if (rhs.is_wrapping()) {
          return (e - rhs.range_to() < rhs.range_from() - e)
                     ? Range(rhs.range_from(), e)
                     : Range(e, rhs.range_to());
        }
        return Range(std::min(e, rhs.range_from()),
                     std::max(e, rhs.range_to()));
      }

      // TODO(nicohartmann@): A wrapping range may be a better fit in some
      // cases.
      auto lhs_range = WordType::Range(lhs.unsigned_min(), lhs.unsigned_max());
      DCHECK(!lhs_range.is_wrapping());
      return LeastUpperBound(lhs_range, rhs, zone);
    }

    // Both sides are sets. We try to construct the combined set.
    std::vector<word_t> result_elements;
    base::vector_append(result_elements, lhs.set_elements());
    base::vector_append(result_elements, rhs.set_elements());
    DCHECK(!result_elements.empty());
    base::sort(result_elements);
    auto it = std::unique(result_elements.begin(), result_elements.end());
    result_elements.erase(it, result_elements.end());
    if (result_elements.size() <= kMaxSetSize) {
      return Set(result_elements, zone);
    }
    // We have to construct a range instead.
    // TODO(nicohartmann@): A wrapping range may be a better fit in some cases.
    return Range(result_elements.front(), result_elements.back());
  } else if (rhs.is_set()) {
    if (rhs.set_size() == 1) {
      word_t e = rhs.set_element(0);
      if (lhs.is_wrapping()) {
        return (e - lhs.range_to() < lhs.range_from() - e)
                   ? Range(lhs.range_from(), e)
                   : Range(e, lhs.range_to());
      }
      return Range(std::min(e, lhs.range_from()), std::max(e, lhs.range_to()));
    }
    // TODO(nicohartmann@): A wrapping range may be a better fit in some cases.
    DCHECK_NE(rhs.unsigned_min(), rhs.unsigned_max());
    auto rhs_range = WordType::Range(rhs.unsigned_min(), rhs.unsigned_max());
    DCHECK(!rhs_range.is_wrapping());
    return LeastUpperBound(lhs, rhs_range, zone);
  }

  const bool lhs_wrapping = lhs.is_wrapping();
  // Case 1: Both ranges non-wrapping
  // lhs ----|XXX|---   ----|XXX|---   --|XXXXXXX|-   -----|XX|---
  // rhs --|XXX|-----   ------|XXX|-   -----|XX|---   --|XXXXXXX|-
  // ==> --|XXXXX|---   ----|XXXXX|-   --|XXXXXXX|-   --|XXXXXXX|-
  if (!lhs_wrapping && !rhs.is_wrapping()) {
    return WordType::Range(std::min(lhs.range_from(), rhs.range_from()),
                           std::max(lhs.range_to(), rhs.range_to()));
  }
  // Case 2: Both ranges wrapping
  // lhs XXX|----|XXX   X|---|XXXXXX   XXXXXX|---|X   XX|--|XXXXXX
  // rhs X|---|XXXXXX   XXX|----|XXX   XX|--|XXXXXX   XXXXXX|--|XX
  // ==> XXX|-|XXXXXX   XXX|-|XXXXXX   XXXXXXXXXXXX   XXXXXXXXXXXX
  if (lhs_wrapping && rhs.is_wrapping()) {
    const auto from = std::min(lhs.range_from(), rhs.range_from());
    const auto to = std::max(lhs.range_to(), rhs.range_to());
    if (to >= from) return WordType::Complete();
    auto result = WordType::Range(from, to);
    DCHECK(result.is_wrapping());
    return result;
  }

  const auto& x = lhs_wrapping ? lhs : rhs;
  const auto& y = lhs_wrapping ? rhs : lhs;
  DCHECK(x.is_wrapping());
  DCHECK(!y.is_wrapping());
  // Case 3 & 4: x is wrapping, y is not
  // x   XXX|----|XXX   XXX|----|XXX   XXXXX|--|XXX   X|-------|XX
  // y   -------|XX|-   -|XX|-------   ----|XXXXX|-   ---|XX|-----
  // ==> XXX|---|XXXX   XXXX|---|XXX   XXXXXXXXXXXX   XXXXXX|--|XX
  if (y.range_from() <= x.range_to()) {
    if (y.range_to() <= x.range_to()) return x;  // y covered by x
    if (y.range_to() >= x.range_from()) return WordType::Complete();  // ex3
    auto result = WordType::Range(x.range_from(), y.range_to());      // ex 1
    DCHECK(result.is_wrapping());
    DCHECK(!result.is_complete());
    return result;
  } else if (y.range_to() >= x.range_from()) {
    if (y.range_from() >= x.range_from()) return x;  // y covered by x
    DCHECK_GT(y.range_from(), x.range_to());         // handled above
    auto result = WordType::Range(y.range_from(), x.range_to());  // ex 2
    DCHECK(result.is_wrapping());
    DCHECK(!result.is_complete());
    return result;
  } else {
    const auto df = y.range_from() - x.range_to();
    const auto dt = x.range_from() - y.range_to();
    WordType<Bits> result =
        df > dt ? WordType::Range(y.range_from(), x.range_to())  // ex 4
                : WordType::Range(x.range_from(), y.range_to());
    DCHECK(result.is_wrapping());
    DCHECK(!result.is_complete());
    return result;
  }
}

template <size_t Bits>
Type WordType<Bits>::Intersect(const WordType<Bits>& lhs,
                               const WordType<Bits>& rhs, Zone* zone) {
  if (lhs.is_complete()) return rhs;
  if (rhs.is_complete()) return lhs;

  if (lhs.is_set() || rhs.is_set()) {
    const auto& x = lhs.is_set() ? lhs : rhs;
    const auto& y = lhs.is_set() ? rhs : lhs;
    std::vector<word_t> result_elements;
    for (int i = 0; i < x.set_size(); ++i) {
      const word_t element = x.set_element(i);
      if (y.Contains(element)) result_elements.push_back(element);
    }
    if (result_elements.empty()) return Type::None();
    DCHECK(detail::is_unique_and_sorted(result_elements));
    return Set(result_elements, zone);
  }

  DCHECK(lhs.is_range() && rhs.is_range());
  const bool lhs_wrapping = lhs.is_wrapping();
  if (!lhs_wrapping && !rhs.is_wrapping()) {
    const auto result_from = std::max(lhs.range_from(), rhs.range_from());
    const auto result_to = std::min(lhs.range_to(), rhs.range_to());
    return result_to < result_from ? Type::None()
                                   : WordType::Range(result_from, result_to);
  }

  if (lhs_wrapping && rhs.is_wrapping()) {
    const auto result_from = std::max(lhs.range_from(), rhs.range_from());
    const auto result_to = std::min(lhs.range_to(), rhs.range_to());
    auto result = WordType::Range(result_from, result_to);
    DCHECK(result.is_wrapping());
    return result;
  }

  const auto& x = lhs_wrapping ? lhs : rhs;
  const auto& y = lhs_wrapping ? rhs : lhs;
  DCHECK(x.is_wrapping());
  DCHECK(!y.is_wrapping());
  auto result = Intersect(y, WordType::Range(0, x.range_to()), zone);
  if (result.IsNone()) return result;
  DCHECK(result.template IsWord<Bits>());
  return Intersect(
      result.template AsWord<Bits>(),
      WordType::Range(x.range_from(), std::numeric_limits<word_t>::max()),
      zone);
}

template <size_t Bits>
void WordType<Bits>::PrintTo(std::ostream& stream) const {
  stream << (Bits == 32 ? "Word32" : "Word64");
  switch (sub_kind()) {
    case SubKind::kRange:
      stream << "[" << range_from() << ", " << range_to() << "]";
      break;
    case SubKind::kSet:
      stream << "{";
      for (int i = 0; i < set_size(); ++i) {
        if (i != 0) stream << ", ";
        stream << set_element(i);
      }
      stream << "}";
      break;
  }
}

template <size_t Bits>
Handle<TurboshaftType> WordType<Bits>::AllocateOnHeap(Factory* factory) const {
  if constexpr (Bits == 32) {
    if (is_range()) {
      return factory->NewTurboshaftWord32RangeType(range_from(), range_to(),
                                                   AllocationType::kYoung);
    } else {
      DCHECK(is_set());
      auto result = factory->NewTurboshaftWord32SetType(set_size(),
                                                        AllocationType::kYoung);
      for (int i = 0; i < set_size(); ++i) {
        result->set_elements(i, set_element(i));
      }
      return result;
    }
  } else {
    if (is_range()) {
      const auto [from_high, from_low] = uint64_to_high_low(range_from());
      const auto [to_high, to_low] = uint64_to_high_low(range_to());
      return factory->NewTurboshaftWord64RangeType(
          from_high, from_low, to_high, to_low, AllocationType::kYoung);
    } else {
      DCHECK(is_set());
      auto result = factory->NewTurboshaftWord64SetType(set_size(),
                                                        AllocationType::kYoung);
      for (int i = 0; i < set_size(); ++i) {
        const auto [high, low] = uint64_to_high_low(set_element(i));
        result->set_elements_high(i, high);
        result->set_elements_low(i, low);
      }
      return result;
    }
  }
}

template <size_t Bits>
bool FloatType<Bits>::Contains(float_t value) const {
  if (std::isnan(value)) return has_nan();
  switch (sub_kind()) {
    case SubKind::kOnlyNan:
      return false;
    case SubKind::kRange: {
      return range_min() <= value && value <= range_max();
    }
    case SubKind::kSet: {
      for (int i = 0; i < set_size(); ++i) {
        if (set_element(i) == value) return true;
      }
      return false;
    }
  }
}

template <size_t Bits>
bool FloatType<Bits>::Equals(const FloatType<Bits>& other) const {
  if (sub_kind() != other.sub_kind()) return false;
  switch (sub_kind()) {
    case SubKind::kOnlyNan:
      return true;
    case SubKind::kRange: {
      return has_nan() == other.has_nan() && range() == other.range();
    }
    case SubKind::kSet: {
      if (has_nan() != other.has_nan() || set_size() != other.set_size()) {
        return false;
      }
      for (int i = 0; i < set_size(); ++i) {
        if (set_element(i) != other.set_element(i)) return false;
      }
      return true;
    }
  }
}

template <size_t Bits>
// static
FloatType<Bits> FloatType<Bits>::LeastUpperBound(const FloatType<Bits>& lhs,
                                                 const FloatType<Bits>& rhs,
                                                 Zone* zone) {
  uint32_t special_values =
      (lhs.has_nan() || rhs.has_nan()) ? Special::kNaN : 0;
  if (lhs.is_complete() || rhs.is_complete()) {
    return Complete(special_values);
  }

  const bool lhs_finite = lhs.is_set() || lhs.is_only_nan();
  const bool rhs_finite = rhs.is_set() || rhs.is_only_nan();

  if (lhs_finite && rhs_finite) {
    std::vector<float_t> result_elements;
    if (lhs.is_set()) base::vector_append(result_elements, lhs.set_elements());
    if (rhs.is_set()) base::vector_append(result_elements, rhs.set_elements());
    if (result_elements.empty()) {
      DCHECK_EQ(special_values, Special::kNaN);
      return NaN();
    }
    base::sort(result_elements);
    auto it = std::unique(result_elements.begin(), result_elements.end());
    result_elements.erase(it, result_elements.end());
    if (result_elements.size() <= kMaxSetSize) {
      return Set(result_elements, special_values, zone);
    }
    return Range(result_elements.front(), result_elements.back(),
                 special_values);
  }

  // We need to construct a range.
  float_t result_min = std::min(lhs.min(), rhs.min());
  float_t result_max = std::max(lhs.max(), rhs.max());
  return Range(result_min, result_max, special_values);
}

template <size_t Bits>
// static
Type FloatType<Bits>::Intersect(const FloatType<Bits>& lhs,
                                const FloatType<Bits>& rhs, Zone* zone) {
  auto UpdateSpecials = [](const FloatType& t, uint32_t special_values) {
    if (t.bitfield_ == special_values) return t;
    auto result = t;
    result.bitfield_ = special_values;
    return result;
  };

  const bool has_nan = lhs.has_nan() && rhs.has_nan();
  if (lhs.is_complete()) return UpdateSpecials(rhs, has_nan ? kNaN : 0);
  if (rhs.is_complete()) return UpdateSpecials(lhs, has_nan ? kNaN : 0);
  if (lhs.is_only_nan() || rhs.is_only_nan()) {
    return has_nan ? NaN() : Type::None();
  }

  if (lhs.is_set() || rhs.is_set()) {
    const auto& x = lhs.is_set() ? lhs : rhs;
    const auto& y = lhs.is_set() ? rhs : lhs;
    std::vector<float_t> result_elements;
    for (int i = 0; i < x.set_size(); ++i) {
      const float_t element = x.set_element(i);
      if (y.Contains(element)) result_elements.push_back(element);
    }
    if (result_elements.empty()) {
      return has_nan ? NaN() : Type::None();
    }
    DCHECK(detail::is_unique_and_sorted(result_elements));
    return Set(result_elements, has_nan ? kNaN : 0, zone);
  }

  DCHECK(lhs.is_range() && rhs.is_range());
  const float_t result_min = std::min(lhs.min(), rhs.min());
  const float_t result_max = std::max(lhs.max(), rhs.max());
  if (result_min < result_max) {
    return Range(result_min, result_max, has_nan ? kNaN : 0);
  } else if (result_min == result_max) {
    return Set({result_min}, has_nan ? kNaN : 0, zone);
  }
  return has_nan ? NaN() : Type::None();
}

template <size_t Bits>
void FloatType<Bits>::PrintTo(std::ostream& stream) const {
  stream << (Bits == 32 ? "Float32" : "Float64");
  switch (sub_kind()) {
    case SubKind::kOnlyNan:
      stream << "NaN";
      break;
    case SubKind::kRange:
      stream << "[" << range_min() << ", " << range_max()
             << (has_nan() ? "]+NaN" : "]");
      break;
    case SubKind::kSet:
      stream << "{";
      for (int i = 0; i < set_size(); ++i) {
        if (i != 0) stream << ", ";
        stream << set_element(i);
      }
      stream << (has_nan() ? "}+NaN" : "}");
      break;
  }
}

template <size_t Bits>
Handle<TurboshaftType> FloatType<Bits>::AllocateOnHeap(Factory* factory) const {
  float_t min = 0.0f, max = 0.0f;
  if (is_only_nan()) {
    min = std::numeric_limits<float_t>::infinity();
    max = -std::numeric_limits<float_t>::infinity();
    return factory->NewTurboshaftFloat64RangeType(1, min, max,
                                                  AllocationType::kYoung);
  } else if (is_range()) {
    std::tie(min, max) = minmax();
    return factory->NewTurboshaftFloat64RangeType(has_nan() ? 1 : 0, min, max,
                                                  AllocationType::kYoung);
  } else {
    DCHECK(is_set());
    auto result = factory->NewTurboshaftFloat64SetType(
        has_nan() ? 1 : 0, set_size(), AllocationType::kYoung);
    for (int i = 0; i < set_size(); ++i) {
      result->set_elements(i, set_element(i));
    }
    return result;
  }
}

template class WordType<32>;
template class WordType<64>;
template class FloatType<32>;
template class FloatType<64>;

}  // namespace v8::internal::compiler::turboshaft
