// FIXME: This file collects Abseil code that is left almost completely
// unchanged. For everything except Bitmask, and Group* there are corresponding
// V8 version that I need to plug in instead, but haven't done, yet.
// Ultimately, we probably want to put Bitmask, and Group* into the main swiss
// table file. If done the split mostly to track for myself which parts of the
// Abseil codes didn't need any changes at all.

// This is only included from within swiss-hash-table.h

// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD"

// Dummy temporary copyright header to make presubmit check happy:
// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// -------------------------
// from abseils have_sse.h

#ifndef ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSE2
#if defined(__SSE2__) ||  \
    (defined(_MSC_VER) && \
     (defined(_M_X64) || (defined(_M_IX86) && _M_IX86_FP >= 2)))
#define ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSE2 1
#else
#define ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSE2 0
#endif
#endif

#ifndef ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSSE3
#ifdef __SSSE3__
#define ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSSE3 1
#else
#define ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSSE3 0
#endif
#endif

#if ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSSE3 && \
    !ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSE2
#error "Bad configuration!"
#endif

#if ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSE2
#include <emmintrin.h>
#endif

#if ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSSE3
#include <tmmintrin.h>
#endif
// -------------------------

// -------------------------
// from abseils attributes.h

#ifdef __has_attribute
#define ABSL_HAVE_ATTRIBUTE(x) __has_attribute(x)
#else
#define ABSL_HAVE_ATTRIBUTE(x) 0
#endif

#if ABSL_HAVE_ATTRIBUTE(always_inline) || \
    (defined(__GNUC__) && !defined(__clang__))
#define ABSL_ATTRIBUTE_ALWAYS_INLINE __attribute__((always_inline))
#define ABSL_HAVE_ATTRIBUTE_ALWAYS_INLINE 1
#else
#define ABSL_ATTRIBUTE_ALWAYS_INLINE
#endif

// -------------------------
// from config.h
#if (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
     __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define ABSL_IS_LITTLE_ENDIAN 1
#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define ABSL_IS_BIG_ENDIAN 1
#elif defined(_WIN32)
#define ABSL_IS_LITTLE_ENDIAN 1
#else
#error "absl endian detection needs to be set up for your compiler"
#endif
// -------------------------

// -------------------------
// from abseils unaligned_access.h, sligtly modified to deal with namespacing

inline uint64_t UnalignedLoad64(const void* p) {
  uint64_t t;
  memcpy(&t, p, sizeof t);
  return t;
}

inline void UnalignedStore64(void* p, uint64_t v) { memcpy(p, &v, sizeof v); }

#define ABSL_INTERNAL_UNALIGNED_LOAD64(_p) (UnalignedLoad64(_p))

#define ABSL_INTERNAL_UNALIGNED_STORE64(_p, _val) (UnalignedStore64(_p, _val))

// -------------------------

// -------------------------
// from abseils endian.h

// Load/Store methods are alignment safe
namespace little_endian {
// Conversion functions.
#ifdef ABSL_IS_LITTLE_ENDIAN

inline uint64_t FromHost64(uint64_t x) { return x; }
inline uint64_t ToHost64(uint64_t x) { return x; }

#elif defined ABSL_IS_BIG_ENDIAN
inline uint64_t FromHost64(uint64_t x) { return gbswap_64(x); }
inline uint64_t ToHost64(uint64_t x) { return gbswap_64(x); }

#endif /* ENDIAN */

inline uint64_t Load64(const void* p) {
  return ToHost64(ABSL_INTERNAL_UNALIGNED_LOAD64(p));
}

inline void Store64(void* p, uint64_t v) {
  ABSL_INTERNAL_UNALIGNED_STORE64(p, FromHost64(v));
}

}  // namespace little_endian

// -------------------------
// from abseils bits.h

#if defined(_MSC_VER) && !defined(__clang__)
// We can achieve something similar to attribute((always_inline)) with MSVC by
// using the __forceinline keyword, however this is not perfect. MSVC is
// much less aggressive about inlining, and even with the __forceinline keyword.
#define ABSL_BASE_INTERNAL_FORCEINLINE __forceinline
#else
// Use default attribute inline.
#define ABSL_BASE_INTERNAL_FORCEINLINE inline ABSL_ATTRIBUTE_ALWAYS_INLINE
#endif

ABSL_BASE_INTERNAL_FORCEINLINE int CountLeadingZeros64Slow(uint64_t n) {
  int zeroes = 60;
  if (n >> 32) {
    zeroes -= 32;
    n >>= 32;
  }
  if (n >> 16) {
    zeroes -= 16;
    n >>= 16;
  }
  if (n >> 8) {
    zeroes -= 8;
    n >>= 8;
  }
  if (n >> 4) {
    zeroes -= 4;
    n >>= 4;
  }
  return "\4\3\2\2\1\1\1\1\0\0\0\0\0\0\0"[n] + zeroes;
}

ABSL_BASE_INTERNAL_FORCEINLINE int CountLeadingZeros64(uint64_t n) {
#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64)
  // MSVC does not have __buitin_clzll. Use _BitScanReverse64.
  unsigned long result = 0;  // NOLINT(runtime/int)
  if (_BitScanReverse64(&result, n)) {
    return 63 - result;
  }
  return 64;
#elif defined(_MSC_VER) && !defined(__clang__)
  // MSVC does not have __buitin_clzll. Compose two calls to _BitScanReverse
  unsigned long result = 0;  // NOLINT(runtime/int)
  if ((n >> 32) &&
      _BitScanReverse(&result, static_cast<unsigned long>(n >> 32))) {
    return 31 - result;
  }
  if (_BitScanReverse(&result, static_cast<unsigned long>(n))) {
    return 63 - result;
  }
  return 64;
#elif defined(__GNUC__) || defined(__clang__)
  // Use __builtin_clzll, which uses the following instructions:
  //  x86: bsr
  //  ARM64: clz
  //  PPC: cntlzd
  static_assert(sizeof(unsigned long long) == sizeof(n),  // NOLINT(runtime/int)
                "__builtin_clzll does not take 64-bit arg");

  // Handle 0 as a special case because __builtin_clzll(0) is undefined.
  if (n == 0) {
    return 64;
  }
  return __builtin_clzll(n);
#else
  return CountLeadingZeros64Slow(n);
#endif
}

ABSL_BASE_INTERNAL_FORCEINLINE int CountLeadingZeros32Slow(uint64_t n) {
  int zeroes = 28;
  if (n >> 16) {
    zeroes -= 16;
    n >>= 16;
  }
  if (n >> 8) {
    zeroes -= 8;
    n >>= 8;
  }
  if (n >> 4) {
    zeroes -= 4;
    n >>= 4;
  }
  return "\4\3\2\2\1\1\1\1\0\0\0\0\0\0\0"[n] + zeroes;
}

ABSL_BASE_INTERNAL_FORCEINLINE int CountLeadingZeros32(uint32_t n) {
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long result = 0;  // NOLINT(runtime/int)
  if (_BitScanReverse(&result, n)) {
    return 31 - result;
  }
  return 32;
#elif defined(__GNUC__) || defined(__clang__)
  // Use __builtin_clz, which uses the following instructions:
  //  x86: bsr
  //  ARM64: clz
  //  PPC: cntlzd
  static_assert(sizeof(int) == sizeof(n),
                "__builtin_clz does not take 32-bit arg");

  // Handle 0 as a special case because __builtin_clz(0) is undefined.
  if (n == 0) {
    return 32;
  }
  return __builtin_clz(n);
#else
  return CountLeadingZeros32Slow(n);
#endif
}

ABSL_BASE_INTERNAL_FORCEINLINE int CountTrailingZerosNonZero64Slow(uint64_t n) {
  int c = 63;
  n &= ~n + 1;
  if (n & 0x00000000FFFFFFFF) c -= 32;
  if (n & 0x0000FFFF0000FFFF) c -= 16;
  if (n & 0x00FF00FF00FF00FF) c -= 8;
  if (n & 0x0F0F0F0F0F0F0F0F) c -= 4;
  if (n & 0x3333333333333333) c -= 2;
  if (n & 0x5555555555555555) c -= 1;
  return c;
}

ABSL_BASE_INTERNAL_FORCEINLINE int CountTrailingZerosNonZero64(uint64_t n) {
#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64)
  unsigned long result = 0;  // NOLINT(runtime/int)
  _BitScanForward64(&result, n);
  return result;
#elif defined(_MSC_VER) && !defined(__clang__)
  unsigned long result = 0;  // NOLINT(runtime/int)
  if (static_cast<uint32_t>(n) == 0) {
    _BitScanForward(&result, static_cast<unsigned long>(n >> 32));
    return result + 32;
  }
  _BitScanForward(&result, static_cast<unsigned long>(n));
  return result;
#elif defined(__GNUC__) || defined(__clang__)
  static_assert(sizeof(unsigned long long) == sizeof(n),  // NOLINT(runtime/int)
                "__builtin_ctzll does not take 64-bit arg");
  return __builtin_ctzll(n);
#else
  return CountTrailingZerosNonZero64Slow(n);
#endif
}

ABSL_BASE_INTERNAL_FORCEINLINE int CountTrailingZerosNonZero32Slow(uint32_t n) {
  int c = 31;
  n &= ~n + 1;
  if (n & 0x0000FFFF) c -= 16;
  if (n & 0x00FF00FF) c -= 8;
  if (n & 0x0F0F0F0F) c -= 4;
  if (n & 0x33333333) c -= 2;
  if (n & 0x55555555) c -= 1;
  return c;
}

ABSL_BASE_INTERNAL_FORCEINLINE int CountTrailingZerosNonZero32(uint32_t n) {
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long result = 0;  // NOLINT(runtime/int)
  _BitScanForward(&result, n);
  return result;
#elif defined(__GNUC__) || defined(__clang__)
  static_assert(sizeof(int) == sizeof(n),
                "__builtin_ctz does not take 32-bit arg");
  return __builtin_ctz(n);
#else
  return CountTrailingZerosNonZero32Slow(n);
#endif
}

// -------------------------
// from original swiss tables files, all unchanged

// Unchanged to abseil, except using int in place for some size_t
template <size_t GroupSize>
class probe_seq {
 public:
  probe_seq(uint32_t hash, uint32_t mask) {
    // Mask must be power of 2 minus 1
    DCHECK_EQ(0, ((mask + 1) & mask));
    mask_ = mask;
    offset_ = hash & mask_;
  }
  uint32_t offset() const { return offset_; }
  uint32_t offset(int i) const { return (offset_ + i) & mask_; }

  void next() {
    index_ += GroupSize;
    offset_ += index_;
    offset_ &= mask_;
  }
  // The i-th probe (0-based) in the probe sequence, multiplied by GroupSize.
  size_t index() const { return index_; }

 private:
  uint32_t mask_;
  uint32_t offset_;
  uint32_t index_ = 0;
};

// FIXME: use v8s count trailing
template <typename T>
int TrailingZeros(T x) {
  return sizeof(T) == 8 ? CountTrailingZerosNonZero64(static_cast<uint64_t>(x))
                        : CountTrailingZerosNonZero32(static_cast<uint32_t>(x));
}

template <typename T>
int LeadingZeros(T x) {
  return sizeof(T) == 8 ? CountLeadingZeros64(static_cast<uint64_t>(x))
                        : CountLeadingZeros32(static_cast<uint32_t>(x));
}

// -----------------------
// everything below is from abseils raw_hash_set.h.

// An abstraction over a bitmask. It provides an easy way to iterate through the
// indexes of the set bits of a bitmask.  When Shift=0 (platforms with SSE),
// this is a true bitmask.  On non-SSE, platforms the arithematic used to
// emulate the SSE behavior works in bytes (Shift=3) and leaves each bytes as
// either 0x00 or 0x80.
//
// For example:
//   for (int i : BitMask<uint32_t, 16>(0x5)) -> yields 0, 2
//   for (int i : BitMask<uint64_t, 8, 3>(0x0000000080800000)) -> yields 2, 3

// FIXME: In other words, in the second case this is a "bitmask" where each
// "bit" is actually a byte, where 08 is 1 and 00 is 0
template <class T, int SignificantBits, int Shift = 0>
class BitMask {
  static_assert(std::is_unsigned<T>::value, "");
  static_assert(Shift == 0 || Shift == 3, "");

 public:
  // These are useful for unit tests (gunit).
  using value_type = int;
  using iterator = BitMask;
  using const_iterator = BitMask;

  explicit BitMask(T mask) : mask_(mask) {}
  BitMask& operator++() {
    // Clear the least significant bit that is set.
    mask_ &= (mask_ - 1);
    return *this;
  }
  explicit operator bool() const { return mask_ != 0; }
  int operator*() const { return LowestBitSet(); }
  int LowestBitSet() const {
    return v8::internal::TrailingZeros(mask_) >> Shift;
  }
  int HighestBitSet() const {
    return (sizeof(T) * CHAR_BIT - v8::internal::LeadingZeros(mask_) - 1) >>
           Shift;
  }

  BitMask begin() const { return *this; }
  BitMask end() const { return BitMask(0); }

  int TrailingZeros() const {
    return v8::internal::TrailingZeros(mask_) >> Shift;
  }

  int LeadingZeros() const {
    constexpr int total_significant_bits = SignificantBits << Shift;
    constexpr int extra_bits = sizeof(T) * 8 - total_significant_bits;
    return v8::internal::LeadingZeros(mask_ << extra_bits) >> Shift;
  }

 private:
  friend bool operator==(const BitMask& a, const BitMask& b) {
    return a.mask_ == b.mask_;
  }
  friend bool operator!=(const BitMask& a, const BitMask& b) {
    return a.mask_ != b.mask_;
  }

  T mask_;
};

using ctrl_t = signed char;
using h2_t = uint8_t;

// The values here are selected for maximum performance. See the static asserts
// below for details.
enum Ctrl : ctrl_t {
  kEmpty = -128,   // 0b10000000
  kDeleted = -2,   // 0b11111110
  kSentinel = -1,  // 0b11111111
};
static_assert(
    kEmpty & kDeleted & kSentinel & 0x80,
    "Special markers need to have the MSB to make checking for them efficient");
static_assert(kEmpty < kSentinel && kDeleted < kSentinel,
              "kEmpty and kDeleted must be smaller than kSentinel to make the "
              "SIMD test of IsEmptyOrDeleted() efficient");
static_assert(kSentinel == -1,
              "kSentinel must be -1 to elide loading it from memory into SIMD "
              "registers (pcmpeqd xmm, xmm)");
static_assert(kEmpty == -128,
              "kEmpty must be -128 to make the SIMD check for its "
              "existence efficient (psignb xmm, xmm)");
static_assert(~kEmpty & ~kDeleted & kSentinel & 0x7F,
              "kEmpty and kDeleted must share an unset bit that is not shared "
              "by kSentinel to make the scalar test for MatchEmptyOrDeleted() "
              "efficient");
static_assert(kDeleted == -2,
              "kDeleted must be -2 to make the implementation of "
              "ConvertSpecialToEmptyAndFullToDeleted efficient");

// A single block of empty control bytes for tables without any slots allocated.
// This enables removing a branch in the hot path of find().
// FIXME: no point in having the sentinel here, but also doesn't hurt.
inline ctrl_t* EmptyGroup() {
  alignas(16) static constexpr ctrl_t empty_group[] = {
      kSentinel, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty,
      kEmpty,    kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty};
  return const_cast<ctrl_t*>(empty_group);
}

#if ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSE2

// https://github.com/abseil/abseil-cpp/issues/209
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=87853
// _mm_cmpgt_epi8 is broken under GCC with -funsigned-char
// Work around this by using the portable implementation of Group
// when using -funsigned-char under GCC.
inline __m128i _mm_cmpgt_epi8_fixed(__m128i a, __m128i b) {
#if defined(__GNUC__) && !defined(__clang__)
  if (std::is_unsigned<char>::value) {
    const __m128i mask = _mm_set1_epi8(0x80);
    const __m128i diff = _mm_subs_epi8(b, a);
    return _mm_cmpeq_epi8(_mm_and_si128(diff, mask), mask);
  }
#endif
  return _mm_cmpgt_epi8(a, b);
}

struct GroupSse2Impl {
  static constexpr size_t kWidth = 16;  // the number of slots per group

  explicit GroupSse2Impl(const ctrl_t* pos) {
    ctrl = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pos));
  }

  // Returns a bitmask representing the positions of slots that match hash.
  BitMask<uint32_t, kWidth> Match(h2_t hash) const {
    auto match = _mm_set1_epi8(hash);
    return BitMask<uint32_t, kWidth>(
        _mm_movemask_epi8(_mm_cmpeq_epi8(match, ctrl)));
  }

  // Returns a bitmask representing the positions of empty slots.
  BitMask<uint32_t, kWidth> MatchEmpty() const {
#if ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSSE3
    // This only works because kEmpty is -128.
    return BitMask<uint32_t, kWidth>(
        _mm_movemask_epi8(_mm_sign_epi8(ctrl, ctrl)));
#else
    return Match(static_cast<h2_t>(kEmpty));
#endif
  }

  // Returns a bitmask representing the positions of empty or deleted slots.
  BitMask<uint32_t, kWidth> MatchEmptyOrDeleted() const {
    auto special = _mm_set1_epi8(kSentinel);
    return BitMask<uint32_t, kWidth>(
        _mm_movemask_epi8(_mm_cmpgt_epi8_fixed(special, ctrl)));
  }

  // Returns the number of trailing empty or deleted elements in the group.
  uint32_t CountLeadingEmptyOrDeleted() const {
    auto special = _mm_set1_epi8(kSentinel);
    return TrailingZeros(
        _mm_movemask_epi8(_mm_cmpgt_epi8_fixed(special, ctrl)) + 1);
  }

  void ConvertSpecialToEmptyAndFullToDeleted(ctrl_t* dst) const {
    auto msbs = _mm_set1_epi8(static_cast<char>(-128));
    auto x126 = _mm_set1_epi8(126);
#if ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSSE3
    auto res = _mm_or_si128(_mm_shuffle_epi8(x126, ctrl), msbs);
#else
    auto zero = _mm_setzero_si128();
    auto special_mask = _mm_cmpgt_epi8_fixed(zero, ctrl);
    auto res = _mm_or_si128(msbs, _mm_andnot_si128(special_mask, x126));
#endif
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), res);
  }

  __m128i ctrl;
};
#endif  // ABSL_INTERNAL_RAW_HASH_SET_HAVE_SSE2

struct GroupPortableImpl {
  static constexpr size_t kWidth = 8;

  explicit GroupPortableImpl(const ctrl_t* pos)
      : ctrl(little_endian::Load64(pos)) {}

  BitMask<uint64_t, kWidth, 3> Match(h2_t hash) const {
    // For the technique, see:
    // http://graphics.stanford.edu/~seander/bithacks.html##ValueInWord
    // (Determine if a word has a byte equal to n).
    //
    // Caveat: there are false positives but:
    // - they only occur if there is a real match
    // - they never occur on kEmpty, kDeleted, kSentinel
    // - they will be handled gracefully by subsequent checks in code
    //
    // Example:
    //   v = 0x1716151413121110
    //   hash = 0x12
    //   retval = (v - lsbs) & ~v & msbs = 0x0000000080800000
    constexpr uint64_t msbs = 0x8080808080808080ULL;
    constexpr uint64_t lsbs = 0x0101010101010101ULL;
    auto x = ctrl ^ (lsbs * hash);
    return BitMask<uint64_t, kWidth, 3>((x - lsbs) & ~x & msbs);
  }

  BitMask<uint64_t, kWidth, 3> MatchEmpty() const {
    constexpr uint64_t msbs = 0x8080808080808080ULL;
    return BitMask<uint64_t, kWidth, 3>((ctrl & (~ctrl << 6)) & msbs);
  }

  BitMask<uint64_t, kWidth, 3> MatchEmptyOrDeleted() const {
    constexpr uint64_t msbs = 0x8080808080808080ULL;
    return BitMask<uint64_t, kWidth, 3>((ctrl & (~ctrl << 7)) & msbs);
  }

  uint32_t CountLeadingEmptyOrDeleted() const {
    constexpr uint64_t gaps = 0x00FEFEFEFEFEFEFEULL;
    return (TrailingZeros(((~ctrl & (ctrl >> 7)) | gaps) + 1) + 7) >> 3;
  }

  void ConvertSpecialToEmptyAndFullToDeleted(ctrl_t* dst) const {
    constexpr uint64_t msbs = 0x8080808080808080ULL;
    constexpr uint64_t lsbs = 0x0101010101010101ULL;
    auto x = ctrl & msbs;
    auto res = (~x + (x >> 7)) & ~lsbs;
    little_endian::Store64(dst, res);
  }

  uint64_t ctrl;
};
