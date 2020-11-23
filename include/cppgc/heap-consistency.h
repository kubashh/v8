// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_HEAP_CONSISTENCY_H_
#define INCLUDE_CPPGC_HEAP_CONSISTENCY_H_

#include <cstddef>

#include "cppgc/trace-trait.h"
#include "v8config.h"  // NOLINT(build/include_directory)

namespace cppgc {

class HeapHandle;

namespace subtle {

/**
 * **DO NOT USE: Use the appropriate managed types.**
 *
 * Consistency helpers that aid in maintaining a consistent internal state of
 * the garbage collector.
 */
class HeapConsistency final {
 public:
  using LazyHeapCallback = HeapHandle&();

  /**
   * Conservative Dijkstra-style write barrier that processes an object if it
   * has not yet been processed.
   *
   * \param slot A slot containing the pointer to the object.
   * \param value The pointer to the object. May be an interior pointer to a
   *   an interface of the actual object.
   */
  static V8_INLINE void DijkstraWriteBarrier(void* slot, void* value);

  /**
   * Conservative Dijkstra-style write barrier that processes a range of
   * elements if they have not yet been processed.
   *
   * \param heap_callback A callback to retrieve the corresponding heap if
   *   necessary.
   * \param first_element Pointer to the first element that should be processed.
   * \param element_size Size of the element in bytes.
   * \param number_of_elements Number of elements that should be processed,
   *   starting with |first_element|.
   * \param trace_callback The trace callback that should be invoked for each
   *   element if necessary.
   */
  static V8_INLINE void DijkstraWriteBarrierRange(
      LazyHeapCallback heap_callback, void* first_element, size_t element_size,
      size_t number_of_elements, TraceCallback trace_callback);

  /**
   * Steele-style write barrier that re-processes an object if it has already
   * been processed.
   *
   * \param slot A slot containing the pointer to the object.
   * \param value The pointer to the object. May be an interior pointer to a
   *   an interface of the actual object.
   */
  static V8_INLINE void SteeleWriteBarrier(void* slot, void* value);

 private:
  HeapConsistency() = delete;
};

void HeapConsistency::DijkstraWriteBarrier(void* slot, void* value) {}

void HeapConsistency::DijkstraWriteBarrierRange(LazyHeapCallback heap_callback,
                                                void* first_element,
                                                size_t element_size,
                                                size_t number_of_elements,
                                                TraceCallback trace_callback) {}

void HeapConsistency::SteeleWriteBarrier(void* slot, void* value) {}

}  // namespace subtle
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_HEAP_CONSISTENCY_H_
