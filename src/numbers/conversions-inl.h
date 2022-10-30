// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_NUMBERS_CONVERSIONS_INL_H_
#define V8_NUMBERS_CONVERSIONS_INL_H_

#include <float.h>   // Required for DBL_MAX and on Win32 for finite()
#include <limits.h>  // Required for INT_MAX etc.
#include <stdarg.h>
#include <cmath>
#include "src/common/globals.h"  // Required for V8_INFINITY

// ----------------------------------------------------------------------------
// Extra POSIX/ANSI functions for Win32/MSVC.

#include "src/base/bits.h"
#include "src/base/numbers/double.h"
#include "src/base/platform/platform.h"
#include "src/numbers/conversions.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/objects-inl.h"

#if V8_TARGET_ARCH_X64
#include <emmintrin.h>
#endif

namespace v8 {
namespace internal {

#if V8_TARGET_ARCH_X64
// The fast double-to-unsigned-int conversion routine does not guarantee
// rounding towards zero, or any reasonable value if the argument is larger
// than what fits in an unsigned 32-bit integer.
inline unsigned int FastD2UI(double x) {
  const double k2Pow52 = 4503599627370496.0;
  __m128d xVect = _mm_set_sd(x);
  __m128d absX = _mm_and_pd(
      xVect, _mm_castsi128_pd(_mm_set_epi64x(0, 0x7FFFFFFFFFFFFFFF)));

  // Check to see if |x| < k2Pow52
  __m128d inRangeMask = _mm_cmplt_sd(absX, _mm_set_sd(k2Pow52));

  // Set xToConv to x if |x| < k2Pow52 is true
  __m128d xToConv = _mm_and_pd(xVect, inRangeMask);

  // Set xToConv to 2147483648 if |x| < k2Pow52 is false
  xToConv =
      _mm_or_pd(xToConv, _mm_andnot_pd(inRangeMask, _mm_set_sd(2147483648.0)));

  // First convert xToConv to a 64-bit signed integer,
  // and then truncate the 64-bit signed integer to a 32-bit unsigned
  // integer.
  return static_cast<unsigned int>(_mm_cvttsd_si64(xToConv));
}
#else
// The fast double-to-unsigned-int conversion routine does not guarantee
// rounding towards zero, or any reasonable value if the argument is larger
// than what fits in an unsigned 32-bit integer.
inline unsigned int FastD2UI(double x) {
  // There is no unsigned version of lrint, so there is no fast path
  // in this function as there is in FastD2I. Using lrint doesn't work
  // for values of 2^31 and above.

  // Convert "small enough" doubles to uint32_t by fixing the 32
  // least significant non-fractional bits in the low 32 bits of the
  // double, and reading them from there.
  const double k2Pow52 = 4503599627370496.0;
  bool negative = x < 0;
  if (negative) {
    x = -x;
  }
  if (x < k2Pow52) {
    x += k2Pow52;
    uint32_t result;
#ifndef V8_TARGET_BIG_ENDIAN
    void* mantissa_ptr = reinterpret_cast<void*>(&x);
#else
    void* mantissa_ptr =
        reinterpret_cast<void*>(reinterpret_cast<Address>(&x) + kInt32Size);
#endif
    // Copy least significant 32 bits of mantissa.
    memcpy(&result, mantissa_ptr, sizeof(result));
    return negative ? ~result + 1 : result;
  }
  // Large number (outside uint32 range), Infinity or NaN.
  return 0x80000000u;  // Return integer indefinite.
}
#endif  // V8_TARGET_ARCH_X64

inline float DoubleToFloat32(double x) {
  using limits = std::numeric_limits<float>;
  if (x > limits::max()) {
    // kRoundingThreshold is the maximum double that rounds down to
    // the maximum representable float. Its mantissa bits are:
    // 1111111111111111111111101111111111111111111111111111
    // [<--- float range --->]
    // Note the zero-bit right after the float mantissa range, which
    // determines the rounding-down.
    static const double kRoundingThreshold = 3.4028235677973362e+38;
    if (x <= kRoundingThreshold) return limits::max();
    return limits::infinity();
  }
  if (x < limits::lowest()) {
    // Same as above, mirrored to negative numbers.
    static const double kRoundingThreshold = -3.4028235677973362e+38;
    if (x >= kRoundingThreshold) return limits::lowest();
    return -limits::infinity();
  }
  return static_cast<float>(x);
}

#if V8_TARGET_ARCH_X64
// #sec-tointegerorinfinity
inline double DoubleToInteger(double x) {
  __m128d xVect = _mm_set_sd(x);

  // Zero out any NaN value
  xVect = _mm_and_pd(xVect, _mm_cmpord_sd(xVect, xVect));

  __m128i biasedExp = _mm_and_si128(_mm_srli_epi64(_mm_castpd_si128(xVect), 52),
                                    _mm_set_epi64x(0, 0x07FF));

  // Compute the number of fractional bits by doing a 16-bit
  // unsigned saturated subtraction of 1075 - biasedExp. This
  // will ensure that numOfFracBits is equal to zero if
  // biasedExp >= 1075.
  __m128i numOfFracBits = _mm_subs_epu16(_mm_set_epi64x(0, 1075), biasedExp);

  // If numOfFracBits <= 52 is true, set nonFracBitsMask to (-1LL <<
  // numOfFracBits). Otherwise, set nonFracBitsMask to 0.
  __m128d nonFracBitsMask = _mm_castsi128_pd(
      _mm_andnot_si128(_mm_cmpgt_epi32(_mm_shuffle_epi32(numOfFracBits, 0xA0),
                                       _mm_set_epi32(0, 0, 52, 52)),
                       _mm_sll_epi64(_mm_set1_epi64x(-1), numOfFracBits)));

  // Mask out the fractional bits
  xVect = _mm_and_pd(xVect, nonFracBitsMask);

  return _mm_cvtsd_f64(xVect);
}

// Implements https://heycam.github.io/webidl/#abstract-opdef-converttoint for
// the general case (step 1 and steps 8 to 12). Support for Clamp and
// EnforceRange will come in the future.
inline int64_t DoubleToWebIDLInt64(double x) {
  __m128d adjX = _mm_set_sd(x);

  // If x is NaN or |x| >= 9223372036854775808.0, adjust the exponent of
  // x so that 4611686018427387904.0 <= |adjX| <= 9223372036854774784.0 is true

  // Compute expAdj by masking out the exponent bits and then subtracting
  // 0x43D0000000000000 using an unsigned saturated subtraction
  __m128i expAdj = _mm_and_si128(_mm_castpd_si128(adjX),
                                 _mm_set_epi64x(0, 0x7FF0000000000000));

  // Subtracting 0x43D0000000000000 from the exponent bits using
  // 16-bit unsigned saturated subtraction is sufficient here as the lower
  // 52 bits of expAdj have been zeroed out in the previous step.

  // A 16-bit unsigned saturated subtraction will ensure that expAdj is
  // equal to zero if the exponent of x is less than 62.
  expAdj = _mm_subs_epu16(expAdj, _mm_set_epi64x(0, 0x43D0000000000000));

  // Subtract expAdj from the bitwise representation of adjX using 64-bit
  // integer subtraction.
  adjX = _mm_castsi128_pd(_mm_sub_epi64(_mm_castpd_si128(adjX), expAdj));

  // |adjX| <= 9223372036854774784.0 is now true

  // Convert adjX to a 64-bit signed integer
  __m128i resultVal = _mm_cvtsi64_si128(_mm_cvttsd_si64(adjX));

  // Shift resultVal left by (expAdj >> 52).
  // If expAdj >> 52 is greater than 63, resultVal will be zeroed out.
  resultVal = _mm_sll_epi64(resultVal, _mm_srli_epi64(expAdj, 52));

  // Return resultVal
  return _mm_cvtsi128_si64(resultVal);
}

// Implements most of https://tc39.github.io/ecma262/#sec-toint32.
inline int32_t DoubleToInt32(double x) {
  return static_cast<int32_t>(DoubleToWebIDLInt64(x));
}
#else
// #sec-tointegerorinfinity
inline double DoubleToInteger(double x) {
  // ToIntegerOrInfinity normalizes -0 to +0. Special case 0 for performance.
  if (std::isnan(x) || x == 0.0) return 0;
  if (!std::isfinite(x)) return x;
  // Add 0.0 in the truncation case to ensure this doesn't return -0.
  return ((x > 0) ? std::floor(x) : std::ceil(x)) + 0.0;
}

// Implements most of https://tc39.github.io/ecma262/#sec-toint32.
int32_t DoubleToInt32(double x) {
  if ((std::isfinite(x)) && (x <= INT_MAX) && (x >= INT_MIN)) {
    // All doubles within these limits are trivially convertable to an int.
    return static_cast<int32_t>(x);
  }
  base::Double d(x);
  int exponent = d.Exponent();
  uint64_t bits;
  if (exponent < 0) {
    if (exponent <= -base::Double::kSignificandSize) return 0;
    bits = d.Significand() >> -exponent;
  } else {
    if (exponent > 31) return 0;
    // Masking to a 32-bit value ensures that the result of the
    // static_cast<int64_t> below is not the minimal int64_t value,
    // which would overflow on multiplication with d.Sign().
    bits = (d.Significand() << exponent) & 0xFFFFFFFFul;
  }
  return static_cast<int32_t>(d.Sign() * static_cast<int64_t>(bits));
}

// Implements https://heycam.github.io/webidl/#abstract-opdef-converttoint for
// the general case (step 1 and steps 8 to 12). Support for Clamp and
// EnforceRange will come in the future.
inline int64_t DoubleToWebIDLInt64(double x) {
  if ((std::isfinite(x)) && (x <= kMaxSafeInteger) && (x >= kMinSafeInteger)) {
    // All doubles within these limits are trivially convertable to an int.
    return static_cast<int64_t>(x);
  }
  base::Double d(x);
  int exponent = d.Exponent();
  uint64_t bits;
  if (exponent < 0) {
    if (exponent <= -base::Double::kSignificandSize) return 0;
    bits = d.Significand() >> -exponent;
  } else {
    if (exponent > 63) return 0;
    bits = (d.Significand() << exponent);
    int64_t bits_int64 = static_cast<int64_t>(bits);
    if (bits_int64 == std::numeric_limits<int64_t>::min()) {
      return bits_int64;
    }
  }
  return static_cast<int64_t>(d.Sign() * static_cast<int64_t>(bits));
}
#endif  // V8_TARGET_ARCH_X64

inline uint64_t DoubleToWebIDLUint64(double x) {
  return static_cast<uint64_t>(DoubleToWebIDLInt64(x));
}

bool DoubleToSmiInteger(double value, int* smi_int_value) {
  if (!IsSmiDouble(value)) return false;
  *smi_int_value = FastD2I(value);
  DCHECK(Smi::IsValid(*smi_int_value));
  return true;
}

bool IsSmiDouble(double value) {
  return value >= Smi::kMinValue && value <= Smi::kMaxValue &&
         !IsMinusZero(value) && value == FastI2D(FastD2I(value));
}

#if V8_TARGET_ARCH_X64
bool IsInt32Double(double value) {
  __m128d valVect = _mm_set_sd(value);
  __m128d absVal = _mm_and_pd(
      valVect, _mm_castsi128_pd(_mm_set_epi64x(0, 0x7FFFFFFFFFFFFFFF)));
  __m128d xSignBit = _mm_xor_pd(valVect, absVal);

  // Ensure that |valToConv| < 4294967296.0 is true by making
  // sure that the unbiased exponent is less than or equal to 31
  __m128d valToConv = _mm_castsi128_pd(_mm_min_epi16(
      _mm_castpd_si128(absVal), _mm_set_epi64x(0, 0x41EF7FFF7FFF7FFF)));

  // Copy the sign bit of x to valToConv
  valToConv = _mm_or_pd(valToConv, xSignBit);

  // valToConv is equal to x if |x| < 4294967296.0 is true

  // Convert valToConv as follows:
  // 1. Convert valToConv to a 64-bit signed integer (with truncation)
  //    by using _mm_cvttsd_si64.
  // 2. Truncate the 64-bit signed integer to a 32-bit signed integer.
  // 3. Convert the truncated 32-bit signed integer back to a double.
  __m128d int32DblVal = _mm_cvtsi32_sd(
      valToConv, static_cast<int32_t>(_mm_cvttsd_si64(valToConv)));

  // Return true if the bitwise representation of value is equal to
  // the bitwise representation of int32DblVal.
  return _mm_cvtsi128_si64(_mm_castpd_si128(valVect)) ==
         _mm_cvtsi128_si64(_mm_castpd_si128(int32DblVal));
}

bool IsUint32Double(double value) {
  __m128d valVect = _mm_set_sd(value);

  // Ensure that valToConv >= 0 by zeroing out valToConv if
  // value < 0.0 is true
  __m128i isNegMask =
      _mm_srai_epi32(_mm_shuffle_epi32(_mm_castpd_si128(valVect), 0x55), 31);
  __m128d valToConv = _mm_andnot_pd(_mm_castsi128_pd(isNegMask), valVect);

  // Ensure that 0 <= valToConv < 4294967296.0 is true by making
  // sure that the unbiased exponent is less than or equal to 31
  valToConv = _mm_castsi128_pd(_mm_min_epi16(
      _mm_castpd_si128(valToConv), _mm_set_epi64x(0, 0x41EF7FFF7FFF7FFF)));

  // valToConv is equal to x if 0.0 <= x < 4294967296.0 is true

  // Convert valToConv as follows:
  // 1. Convert valToConv to a 64-bit signed integer (with truncation)
  //    by using _mm_cvttsd_si64.
  // 2. Truncate the 64-bit signed integer to a 32-bit unsigned integer.
  // 3. Convert the truncated 32-bit unsigned integer back to a double
  //    by using _mm_cvtsi64_sd (which will convert the 32-bit unsigned
  //    integer to a signed 64-bit integer prior to doing the
  //    integer to double conversion).
  __m128d uint32DblVal = _mm_cvtsi64_sd(
      valToConv, static_cast<uint32_t>(_mm_cvttsd_si64(valToConv)));

  // Return true if the bitwise representation of value is equal to
  // the bitwise representation of uint32DblVal.
  return _mm_cvtsi128_si64(_mm_castpd_si128(valVect)) ==
         _mm_cvtsi128_si64(_mm_castpd_si128(uint32DblVal));
}
#else
bool IsInt32Double(double value) {
  return value >= kMinInt && value <= kMaxInt && !IsMinusZero(value) &&
         value == FastI2D(FastD2I(value));
}

bool IsUint32Double(double value) {
  return !IsMinusZero(value) && value >= 0 && value <= kMaxUInt32 &&
         value == FastUI2D(FastD2UI(value));
}
#endif  // V8_TARGET_ARCH_X64

bool DoubleToUint32IfEqualToSelf(double value, uint32_t* uint32_value) {
  const double k2Pow52 = 4503599627370496.0;
  const uint32_t kValidTopBits = 0x43300000;
  const uint64_t kBottomBitMask = 0x0000'0000'FFFF'FFFF;

  // Add 2^52 to the double, to place valid uint32 values in the low-significant
  // bits of the exponent, by effectively setting the (implicit) top bit of the
  // significand. Note that this addition also normalises 0.0 and -0.0.
  double shifted_value = value + k2Pow52;

  // At this point, a valid uint32 valued double will be represented as:
  //
  // sign = 0
  // exponent = 52
  // significand = 1. 00...00 <value>
  //       implicit^          ^^^^^^^ 32 bits
  //                  ^^^^^^^^^^^^^^^ 52 bits
  //
  // Therefore, we can first check the top 32 bits to make sure that the sign,
  // exponent and remaining significand bits are valid, and only then check the
  // value in the bottom 32 bits.

  uint64_t result = base::bit_cast<uint64_t>(shifted_value);
  if ((result >> 32) == kValidTopBits) {
    *uint32_value = result & kBottomBitMask;
    return FastUI2D(result & kBottomBitMask) == value;
  }
  return false;
}

int32_t NumberToInt32(Object number) {
  if (number.IsSmi()) return Smi::ToInt(number);
  return DoubleToInt32(HeapNumber::cast(number).value());
}

uint32_t NumberToUint32(Object number) {
  if (number.IsSmi()) return Smi::ToInt(number);
  return DoubleToUint32(HeapNumber::cast(number).value());
}

uint32_t PositiveNumberToUint32(Object number) {
  if (number.IsSmi()) {
    int value = Smi::ToInt(number);
    if (value <= 0) return 0;
    return value;
  }
  double value = HeapNumber::cast(number).value();
  // Catch all values smaller than 1 and use the double-negation trick for NANs.
  if (!(value >= 1)) return 0;
  uint32_t max = std::numeric_limits<uint32_t>::max();
  if (value < max) return static_cast<uint32_t>(value);
  return max;
}

int64_t NumberToInt64(Object number) {
  if (number.IsSmi()) return Smi::ToInt(number);
  double d = HeapNumber::cast(number).value();
  if (std::isnan(d)) return 0;
  if (d >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
    return std::numeric_limits<int64_t>::max();
  }
  if (d <= static_cast<double>(std::numeric_limits<int64_t>::min())) {
    return std::numeric_limits<int64_t>::min();
  }
  return static_cast<int64_t>(d);
}

uint64_t PositiveNumberToUint64(Object number) {
  if (number.IsSmi()) {
    int value = Smi::ToInt(number);
    if (value <= 0) return 0;
    return value;
  }
  double value = HeapNumber::cast(number).value();
  // Catch all values smaller than 1 and use the double-negation trick for NANs.
  if (!(value >= 1)) return 0;
  uint64_t max = std::numeric_limits<uint64_t>::max();
  if (value < max) return static_cast<uint64_t>(value);
  return max;
}

bool TryNumberToSize(Object number, size_t* result) {
  // Do not create handles in this function! Don't use SealHandleScope because
  // the function can be used concurrently.
  if (number.IsSmi()) {
    int value = Smi::ToInt(number);
    DCHECK(static_cast<unsigned>(Smi::kMaxValue) <=
           std::numeric_limits<size_t>::max());
    if (value >= 0) {
      *result = static_cast<size_t>(value);
      return true;
    }
    return false;
  } else {
    double value = HeapNumber::cast(number).value();
    // If value is compared directly to the limit, the limit will be
    // casted to a double and could end up as limit + 1,
    // because a double might not have enough mantissa bits for it.
    // So we might as well cast the limit first, and use < instead of <=.
    double maxSize = static_cast<double>(std::numeric_limits<size_t>::max());
    if (value >= 0 && value < maxSize) {
      *result = static_cast<size_t>(value);
      return true;
    } else {
      return false;
    }
  }
}

size_t NumberToSize(Object number) {
  size_t result = 0;
  bool is_valid = TryNumberToSize(number, &result);
  CHECK(is_valid);
  return result;
}

uint32_t DoubleToUint32(double x) {
  return static_cast<uint32_t>(DoubleToInt32(x));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_NUMBERS_CONVERSIONS_INL_H_
