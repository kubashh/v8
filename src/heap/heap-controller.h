// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_CONTROLLER_H_
#define V8_HEAP_HEAP_CONTROLLER_H_

#include <cstddef>
#include "src/allocation.h"
#include "src/heap/heap.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace internal {

class HeapController {
 public:
  HeapController() {}

  // Computes the allocation limit to trigger the next full garbage collection.
  size_t CalculateOldGenerationAllocationLimit(
      bool dampen_limit, size_t old_gen_size, size_t max_old_generation_size,
      double gc_speed, double mutator_speed, size_t new_space_capacity,
      bool should_grow_slowly, bool should_optimize_mem_usage,
      bool should_reduce_mem);

  size_t MinimumAllocationLimitGrowingStep(bool should_optimize_mem_usage);

  // The old space size has to be a multiple of Page::kPageSize.
  // Sizes are in MB.
  static const size_t kMinOldGenerationSize = 128 * Heap::kPointerMultiplier;
  static const size_t kMaxOldGenerationSize = 1024 * Heap::kPointerMultiplier;

 private:
  FRIEND_TEST(HeapController, HeapGrowingFactor);
  FRIEND_TEST(HeapController, MaxHeapGrowingFactor);
  FRIEND_TEST(HeapController, OldGenerationSize);

  V8_EXPORT_PRIVATE static const double kMinHeapGrowingFactor;
  V8_EXPORT_PRIVATE static const double kMaxHeapGrowingFactor;
  V8_EXPORT_PRIVATE static double MaxHeapGrowingFactor(
      size_t max_old_generation_size);
  V8_EXPORT_PRIVATE static double HeapGrowingFactor(double gc_speed,
                                                    double mutator_speed,
                                                    double max_factor);

  static const double kMaxHeapGrowingFactorMemoryConstrained;
  static const double kMaxHeapGrowingFactorIdle;
  static const double kConservativeHeapGrowingFactor;
  static const double kTargetMutatorUtilization;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HEAP_CONTROLLER_H_
