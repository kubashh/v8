// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_CAGED_HEAP_H_
#define INCLUDE_CPPGC_INTERNAL_CAGED_HEAP_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "cppgc/internal/api-constants.h"
#include "cppgc/internal/logging.h"
#include "cppgc/platform.h"
#include "v8config.h"  // NOLINT(build/include_directory)

#if __cpp_lib_bitopts
#include <bit>
#endif  // __cpp_lib_bitopts

namespace cppgc {
namespace internal {

class BasePageHandle;

class V8_EXPORT CagedHeapBase {
 public:
  static constexpr size_t kCagedHeapNormalPageReservationSize =
      api_constants::kCagedHeapReservationSize / 2;

  static V8_INLINE bool IsWithinCage(const void* ptr) {
    CPPGC_DCHECK(g_heap_base_);
    return (reinterpret_cast<uintptr_t>(ptr) &
            ~(api_constants::kCagedHeapReservationAlignment - 1)) ==
           g_heap_base_;
  }

  static V8_INLINE bool AreWithinCage(const void* ptr1, const void* ptr2) {
    static constexpr size_t kHalfWordShift = sizeof(uint32_t) * CHAR_BIT;
    static_assert((1lu << kHalfWordShift) ==
                  api_constants::kCagedHeapReservationSize);
    CPPGC_DCHECK(g_heap_base_);
    return !(((reinterpret_cast<uintptr_t>(ptr1) ^ g_heap_base_) |
              (reinterpret_cast<uintptr_t>(ptr2) ^ g_heap_base_)) >>
             kHalfWordShift);
  }

  static V8_INLINE bool IsWithinNormalPageReservation(const void* ptr) {
    CPPGC_DCHECK(g_heap_base_);
    // TODO: Rework with bit ops.
    return (reinterpret_cast<uintptr_t>(ptr) - g_heap_base_) <
           kCagedHeapNormalPageReservationSize;
  }

  static V8_INLINE bool IsWithinLargePageReservation(const void* ptr) {
    CPPGC_DCHECK(g_heap_base_);
    // TODO: Rework with bit ops.
    auto uptr = reinterpret_cast<uintptr_t>(ptr);
    return (uptr >= g_heap_base_ + kCagedHeapNormalPageReservationSize) &&
           (uptr < g_heap_base_ + api_constants::kCagedHeapReservationSize);
  }

  static V8_INLINE uintptr_t GetBase() { return g_heap_base_; }

  static BasePageHandle* LookupLargePageFromInnerPointer(const void* ptr);

 private:
  friend class CagedHeap;
  static uintptr_t g_heap_base_;
};

#if defined(CPPGC_YOUNG_GENERATION)

// AgeTable is the bytemap needed for the fast generation check in the write
// barrier. AgeTable contains entries that correspond to 4096 bytes memory
// regions (cards). Each entry in the table represents generation of the objects
// that reside on the corresponding card (young, old or mixed).
class V8_EXPORT AgeTable final {
  static constexpr size_t kRequiredSize = 1 * api_constants::kMB;
  static constexpr size_t kAllocationGranularity =
      api_constants::kAllocationGranularity;

 public:
  // Represents age of the objects living on a single card.
  enum class Age : uint8_t { kOld, kYoung, kMixed };
  // When setting age for a range, consider or ignore ages of the adjacent
  // cards.
  enum class AdjacentCardsPolicy : uint8_t { kConsider, kIgnore };

  static constexpr size_t kCardSizeInBytes =
      api_constants::kCagedHeapReservationSize / kRequiredSize;

  void SetAge(uintptr_t cage_offset, Age age) {
    table_[card(cage_offset)] = age;
  }

  void SetAgeForRange(uintptr_t cage_offset_begin, uintptr_t cage_offset_end,
                      Age age, AdjacentCardsPolicy adjacent_cards_policy);

  V8_INLINE Age GetAge(uintptr_t cage_offset) const {
    return table_[card(cage_offset)];
  }

  void Reset(PageAllocator* allocator);

 private:
  V8_INLINE size_t card(uintptr_t offset) const {
    constexpr size_t kGranularityBits =
#if __cpp_lib_bitopts
        std::countr_zero(static_cast<uint32_t>(kCardSizeInBytes));
#elif V8_HAS_BUILTIN_CTZ
        __builtin_ctz(static_cast<uint32_t>(kCardSizeInBytes));
#else   //! V8_HAS_BUILTIN_CTZ
        // Hardcode and check with assert.
        12;
#endif  // !V8_HAS_BUILTIN_CTZ
    static_assert((1 << kGranularityBits) == kCardSizeInBytes);
    const size_t entry = offset >> kGranularityBits;
    CPPGC_DCHECK(table_.size() > entry);
    return entry;
  }

  std::array<Age, kRequiredSize> table_;
};

static_assert(sizeof(AgeTable) == 1 * api_constants::kMB,
              "Size of AgeTable is 1MB");

#endif  // CPPGC_YOUNG_GENERATION

struct CagedHeapLocalData final {
  explicit CagedHeapLocalData(PageAllocator&);

#if 0
  bool is_incremental_marking_in_progress = false;
  bool is_young_generation_enabled = false;
#endif
#if defined(CPPGC_YOUNG_GENERATION)
  AgeTable age_table;
#endif
};

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_INTERNAL_CAGED_HEAP_H_
