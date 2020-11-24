// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_WRITE_BARRIER_H_
#define INCLUDE_CPPGC_INTERNAL_WRITE_BARRIER_H_

#include "cppgc/internal/api-constants.h"
#include "cppgc/internal/process-heap.h"
#include "cppgc/trace-trait.h"
#include "v8config.h"  // NOLINT(build/include_directory)

#if defined(CPPGC_CAGED_HEAP)
#include "cppgc/internal/caged-heap-local-data.h"
#endif

namespace cppgc {

class HeapHandle;

namespace internal {

class V8_EXPORT WriteBarrier final {
 public:
  enum class Type : uint8_t {
    kNone,
    kMarking,
    kGenerational,
  };

  struct Params {
#if V8_ENABLE_CHECKS
    Type type = Type::kNone;
#endif  // !V8_ENABLE_CHECKS
#if defined(CPPGC_CAGED_HEAP)
    uintptr_t start;

    CagedHeapLocalData& caged_heap() const {
      return *reinterpret_cast<CagedHeapLocalData*>(start);
    }
    uintptr_t slot_offset;
    uintptr_t value_offset;
#endif  // CPPGC_CAGED_HEAP
  };

  // Returns the required write barrier for a given `slot` and `value`.
  static V8_INLINE Type GetWriteBarrierType(const void* slot, const void* value,
                                            Params& params);
  // Returns the required write barrier for a given `slot`.
  static V8_INLINE Type GetWriteBarrierType(const void* slot, Params& params);

  static V8_INLINE void DijkstraMarkingBarrier(const Params& params,
                                               const void* object);
  static V8_INLINE void DijkstraMarkingBarrierRange(
      const Params& params, HeapHandle& heap, const void* first_element,
      size_t element_size, size_t number_of_elements,
      TraceCallback trace_callback);
  static V8_INLINE void SteeleMarkingBarrier(const Params& params,
                                             const void* object);
#if defined(CPPGC_YOUNG_GENERATION)
  static V8_INLINE void GenerationalBarrier(const Params& params,
                                            const void* slot);
#else   // !CPPGC_YOUNG_GENERATION
  static V8_INLINE void GenerationalBarrier(const Params& params,
                                            const void* slot) {}
#endif  // CPPGC_YOUNG_GENERATION

 private:
  WriteBarrier() = delete;

#if defined(CPPGC_CAGED_HEAP)
  // The contents of `params` are only valid if `TryCagedHeap()` returns true.
  static V8_INLINE bool TryGetCagedHeap(const void* slot, const void* value,
                                        Params& params);
#endif  // CPPGC_CAGED_HEAP

  static void DijkstraMarkingBarrierSlow(const void* value);
  static void DijkstraMarkingBarrierSlowWithSentinelCheck(const void* value);
  static void DijkstraMarkingBarrierRangeSlow(HeapHandle& heap_handle,
                                              const void* first_element,
                                              size_t element_size,
                                              size_t number_of_elements,
                                              TraceCallback trace_callback);
  static void SteeleMarkingBarrierSlow(const void* value);
  static void SteeleMarkingBarrierSlowWithSentinelCheck(const void* value);

#if V8_ENABLE_CHECKS
  static void CheckParams(Type expected_type, const Params& params);
#else   // !V8_ENABLE_CHECKS
  static void CheckParams(Type expected_type, const Params& params) {}
#endif  // V8_ENABLE_CHECKS

#if defined(CPPGC_YOUNG_GENERATION)
  static V8_INLINE void GenerationalBarrier(CagedHeapLocalData& local_data,
                                            const void* slot,
                                            uintptr_t slot_offset,
                                            uintptr_t value_offset) {
    const AgeTable& age_table = local_data.age_table;

    // Bail out if the slot is in young generation.
    if (V8_LIKELY(age_table[slot_offset] == AgeTable::Age::kYoung)) return;

    GenerationalBarrierSlow(local_data, age_table, slot, value_offset);
  }

  static void GenerationalBarrierSlow(CagedHeapLocalData& local_data,
                                      const AgeTable& ageTable,
                                      const void* slot, uintptr_t value_offset);
#endif  // CPPGC_YOUNG_GENERATION
};

#if defined(CPPGC_CAGED_HEAP)
// static
bool WriteBarrier::TryGetCagedHeap(const void* slot, const void* value,
                                   WriteBarrier::Params& params) {
  params.start = reinterpret_cast<uintptr_t>(value) &
                 ~(api_constants::kCagedHeapReservationAlignment - 1);
  const uintptr_t slot_offset =
      reinterpret_cast<uintptr_t>(slot) - params.start;
  if (slot_offset > api_constants::kCagedHeapReservationSize) {
    // Check if slot is on stack or value is sentinel or nullptr. This relies
    // on the fact that kSentinelPointer is encoded as 0x1.
    return false;
  }
  return true;
}
#endif  // CPPGC_CAGED_HEAP

// static
WriteBarrier::Type WriteBarrier::GetWriteBarrierType(
    const void* slot, const void* value, WriteBarrier::Params& params) {
#if defined(CPPGC_CAGED_HEAP)
  if (!TryGetCagedHeap(slot, value, params)) {
    return Type::kNone;
  }
  if (V8_UNLIKELY(params.caged_heap().is_marking_in_progress)) {
#if V8_ENABLE_CHECKS
    params.type = Type::kMarking;
#endif  // !V8_ENABLE_CHECKS
    return Type::kMarking;
  }
#if defined(CPPGC_YOUNG_GENERATION)
  params.slot_offset = reinterpret_cast<uintptr_t>(slot) - params.start;
  params.value_offset = reinterpret_cast<uintptr_t>(value) - params.start;
#if V8_ENABLE_CHECKS
  params.type = Type::kGenerational;
#endif  // !V8_ENABLE_CHECKS
  return Type::kGenerational;
#else   // !CPPGC_YOUNG_GENERATION
  return Type::kNone;
#endif  // !CPPGC_YOUNG_GENERATION
#endif  //  CPPGC_CAGED_HEAP
  Type type = V8_LIKELY(!ProcessHeap::IsAnyIncrementalOrConcurrentMarking())
                  ? Type::kNone
                  : Type::kMarking;
#if V8_ENABLE_CHECKS
  params.type = type;
#endif  // !V8_ENABLE_CHECKS
  return type;
}

// static
WriteBarrier::Type WriteBarrier::GetWriteBarrierType(
    const void* slot, WriteBarrier::Params& params) {
#if defined(CPPGC_CAGED_HEAP)
  if (!TryGetCagedHeap(slot, slot, params)) {
    return Type::kNone;
  }
  if (V8_UNLIKELY(params.caged_heap().is_marking_in_progress)) {
#if V8_ENABLE_CHECKS
    params.type = Type::kMarking;
#endif  // !V8_ENABLE_CHECKS
    return Type::kMarking;
  }
#if defined(CPPGC_YOUNG_GENERATION)
  params.slot_offset = reinterpret_cast<uintptr_t>(slot) - params.start;
  params.value_offset = 0;
#if V8_ENABLE_CHECKS
  params.type = Type::kGenerational;
#endif  // !V8_ENABLE_CHECKS
  return Type::kGenerational;
#else   // !CPPGC_YOUNG_GENERATION
  return Type::kNone;
#endif  // !CPPGC_YOUNG_GENERATION
#endif  //  CPPGC_CAGED_HEAP
  Type type = V8_LIKELY(!ProcessHeap::IsAnyIncrementalOrConcurrentMarking())
                  ? Type::kNone
                  : Type::kMarking;
#if V8_ENABLE_CHECKS
  params.type = type;
#endif  // !V8_ENABLE_CHECKS
  return type;
}

// static
void WriteBarrier::DijkstraMarkingBarrier(const Params& params,
                                          const void* object) {
  CheckParams(Type::kMarking, params);
#if defined(CPPGC_CAGED_HEAP)
  // Caged heap already filters out sentinels.
  DijkstraMarkingBarrierSlow(object);
#else   // !CPPGC_CAGED_HEAP
  DijkstraMarkingBarrierSlowWithSentinelCheck(object);
#endif  // !CPPGC_CAGED_HEAP
}

// static
void WriteBarrier::DijkstraMarkingBarrierRange(const Params& params,
                                               HeapHandle& heap,
                                               const void* first_element,
                                               size_t element_size,
                                               size_t number_of_elements,
                                               TraceCallback trace_callback) {
  CheckParams(Type::kMarking, params);
  DijkstraMarkingBarrierRangeSlow(heap, first_element, element_size,
                                  number_of_elements, trace_callback);
}

// static
void WriteBarrier::SteeleMarkingBarrier(const Params& params,
                                        const void* object) {
  CheckParams(Type::kMarking, params);
#if defined(CPPGC_CAGED_HEAP)
  // Caged heap already filters out sentinels.
  SteeleMarkingBarrierSlow(object);
#else   // !CPPGC_CAGED_HEAP
  SteeleMarkingBarrierSlowWithSentinelCheck(object);
#endif  // !CPPGC_CAGED_HEAP
}

#if defined(CPPGC_YOUNG_GENERATION)
// static
void WriteBarrier::GenerationalBarrier(const Params& params, const void* slot) {
  CheckParams(Type::kGenerational, params);
  GenerationalBarrier(params.caged_heap(), slot, params.slot_offset,
                      params.value_offset);
}
#endif  // !CPPGC_YOUNG_GENERATION

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_INTERNAL_WRITE_BARRIER_H_
