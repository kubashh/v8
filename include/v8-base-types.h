// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_BASE_TYPES_H_
#define INCLUDE_V8_BASE_TYPES_H_

#include <stddef.h>
#include <stdint.h>

#include "v8-version.h"  // NOLINT(build/include)
#include "v8config.h"    // NOLINT(build/include)

namespace v8 {

namespace internal {

class Object;

/**
 * Configuration of tagging scheme.
 */
const int kApiPointerSize = sizeof(void*);  // NOLINT
const int kApiDoubleSize = sizeof(double);  // NOLINT
const int kApiIntSize = sizeof(int);        // NOLINT
const int kApiInt64Size = sizeof(int64_t);  // NOLINT

// Tag information for HeapObject.
const int kHeapObjectTag = 1;
const int kWeakHeapObjectTag = 3;
const int kHeapObjectTagSize = 2;
const intptr_t kHeapObjectTagMask = (1 << kHeapObjectTagSize) - 1;

// Tag information for Smi.
const int kSmiTag = 0;
const int kSmiTagSize = 1;
const intptr_t kSmiTagMask = (1 << kSmiTagSize) - 1;

template <size_t tagged_ptr_size>
struct SmiTagging;

template <int kSmiShiftSize>
V8_INLINE internal::Object* IntToSmi(int value) {
  int smi_shift_bits = kSmiTagSize + kSmiShiftSize;
  intptr_t tagged_value =
      (static_cast<intptr_t>(value) << smi_shift_bits) | kSmiTag;
  return reinterpret_cast<internal::Object*>(tagged_value);
}

// Smi constants for systems where tagged pointer is a 32-bit value.
template <>
struct SmiTagging<4> {
  enum { kSmiShiftSize = 0, kSmiValueSize = 31 };
  static int SmiShiftSize() { return kSmiShiftSize; }
  static int SmiValueSize() { return kSmiValueSize; }
  V8_INLINE static int SmiToInt(const internal::Object* value) {
    int shift_bits = kSmiTagSize + kSmiShiftSize;
    // Throw away top 32 bits and shift down (requires >> to be sign extending).
    return static_cast<int>(reinterpret_cast<intptr_t>(value)) >> shift_bits;
  }
  V8_INLINE static internal::Object* IntToSmi(int value) {
    return internal::IntToSmi<kSmiShiftSize>(value);
  }
  V8_INLINE static constexpr bool IsValidSmi(intptr_t value) {
    // To be representable as an tagged small integer, the two
    // most-significant bits of 'value' must be either 00 or 11 due to
    // sign-extension. To check this we add 01 to the two
    // most-significant bits, and check if the most-significant bit is 0
    //
    // CAUTION: The original code below:
    // bool result = ((value + 0x40000000) & 0x80000000) == 0;
    // may lead to incorrect results according to the C language spec, and
    // in fact doesn't work correctly with gcc4.1.1 in some cases: The
    // compiler may produce undefined results in case of signed integer
    // overflow. The computation must be done w/ unsigned ints.
    return static_cast<uintptr_t>(value) + 0x40000000U < 0x80000000U;
  }
};

// Smi constants for systems where tagged pointer is a 64-bit value.
template <>
struct SmiTagging<8> {
  enum { kSmiShiftSize = 31, kSmiValueSize = 32 };
  static int SmiShiftSize() { return kSmiShiftSize; }
  static int SmiValueSize() { return kSmiValueSize; }
  V8_INLINE static int SmiToInt(const internal::Object* value) {
    int shift_bits = kSmiTagSize + kSmiShiftSize;
    // Shift down and throw away top 32 bits.
    return static_cast<int>(reinterpret_cast<intptr_t>(value) >> shift_bits);
  }
  V8_INLINE static internal::Object* IntToSmi(int value) {
    return internal::IntToSmi<kSmiShiftSize>(value);
  }
  V8_INLINE static constexpr bool IsValidSmi(intptr_t value) {
    // To be representable as a long smi, the value must be a 32-bit integer.
    return (value == static_cast<int32_t>(value));
  }
};

#if V8_COMPRESS_POINTERS
static_assert(
    kApiPointerSize == kApiInt64Size,
    "Pointer compression can be enabled only for 64-bit architectures");
typedef SmiTagging<4> PlatformSmiTagging;
#else
typedef SmiTagging<kApiPointerSize> PlatformSmiTagging;
#endif

const int kSmiShiftSize = PlatformSmiTagging::kSmiShiftSize;
const int kSmiValueSize = PlatformSmiTagging::kSmiValueSize;
const int kSmiMinValue = (static_cast<unsigned int>(-1)) << (kSmiValueSize - 1);
const int kSmiMaxValue = -(kSmiMinValue + 1);
constexpr bool SmiValuesAre31Bits() { return kSmiValueSize == 31; }
constexpr bool SmiValuesAre32Bits() { return kSmiValueSize == 32; }

}  // namespace internal
}  // namespace v8

#endif  // INCLUDE_V8_H_
