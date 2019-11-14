// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTILS_BIT_FIELD_H_
#define V8_UTILS_BIT_FIELD_H_

#include <stdint.h>

#include "src/base/macros.h"

namespace v8 {
namespace internal {

// ----------------------------------------------------------------------------
// BitField is a help template for encoding and decode bitfield with
// unsigned content.
// Instantiate them via 'using', which is cheaper than deriving a new class:
// using MyBitField = BitField<int, 4, 2, MyEnum>;
// The BitField class is final to enforce this style over derivation.

template <class T, int shift, int size, class U = uint32_t>
class BitField final {
 public:
  STATIC_ASSERT(std::is_unsigned<U>::value);
  STATIC_ASSERT(shift < 8 * sizeof(U));  // Otherwise shifts by {shift} are UB.
  STATIC_ASSERT(size < 8 * sizeof(U));   // Otherwise shifts by {size} are UB.
  STATIC_ASSERT(shift + size <= 8 * sizeof(U));
  STATIC_ASSERT(size > 0);

  using FieldType = T;

  // A type U mask of bit field.  To use all bits of a type U of x bits
  // in a bitfield without compiler warnings we have to compute 2^x
  // without using a shift count of x in the computation.
  static constexpr int kShift = shift;
  static constexpr int kSize = size;
  static constexpr U kMask = ((U{1} << kShift) << kSize) - (U{1} << kShift);
  static constexpr int kLastUsedBit = kShift + kSize - 1;
  static constexpr U kNumValues = U{1} << kSize;

  // Value for the field with all bits set.
  static constexpr T kMax = static_cast<T>(kNumValues - 1);

  template <class T2, int size2>
  using Next = BitField<T2, kShift + kSize, size2, U>;

  // Tells whether the provided value fits into the bit field.
  static constexpr bool is_valid(T value) {
    return (static_cast<U>(value) & ~static_cast<U>(kMax)) == 0;
  }

  // Returns a type U with the bit field value encoded.
  static constexpr U encode(T value) {
#if V8_HAS_CXX14_CONSTEXPR
    DCHECK(is_valid(value));
#endif
    return static_cast<U>(value) << kShift;
  }

  // Returns a type U with the bit field value updated.
  static constexpr U update(U previous, T value) {
    return (previous & ~kMask) | encode(value);
  }

  // Extracts the bit field from the value.
  static constexpr T decode(U value) {
    return static_cast<T>((value & kMask) >> kShift);
  }
};

template <class T, int shift, int size>
using BitField8 = BitField<T, shift, size, uint8_t>;

template <class T, int shift, int size>
using BitField16 = BitField<T, shift, size, uint16_t>;

template <class T, int shift, int size>
using BitField64 = BitField<T, shift, size, uint64_t>;

}  // namespace internal
}  // namespace v8

#endif  // V8_UTILS_BIT_FIELD_H_
