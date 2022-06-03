// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_HEAP_HANDLE_H_
#define INCLUDE_CPPGC_INTERNAL_HEAP_HANDLE_H_

#include "cppgc/internal/api-constants.h"
#include "cppgc/internal/logging.h"
#include "v8config.h"  // NOLINT(build/include_directory)

namespace cppgc {

namespace internal {
class HeapBase;
}

class V8_EXPORT HeapHandle {
 public:
  V8_INLINE bool IsIncrementalMarkingInProgress() const {
    return is_incremental_marking_in_progress_;
  }

  V8_INLINE bool IsYoungGenerationEnabled() const {
    return is_young_generation_enabled_;
  }

 private:
  HeapHandle() = default;
  friend class internal::HeapBase;

  bool is_incremental_marking_in_progress_ = false;
  bool is_young_generation_enabled_ = false;
};

namespace internal {

class BasePageHandle {
 public:
  static V8_INLINE BasePageHandle* FromPayload(void* payload) {
    return reinterpret_cast<BasePageHandle*>(
        (reinterpret_cast<uintptr_t>(payload) &
         ~(api_constants::kPageSize - 1)) +
        api_constants::kGuardPageSize);
  }

  static V8_INLINE const BasePageHandle* FromPayload(const void* payload) {
    return FromPayload(const_cast<void*>(payload));
  }

  HeapHandle* heap_handle() { return &heap_handle_; }
  const HeapHandle* heap_handle() const { return &heap_handle_; }

 protected:
  explicit BasePageHandle(HeapHandle& heap_handle) : heap_handle_(heap_handle) {
    CPPGC_DCHECK(reinterpret_cast<uintptr_t>(this) % api_constants::kPageSize ==
                 api_constants::kGuardPageSize);
  }

  HeapHandle& heap_handle_;
};

}  // namespace internal

}  // namespace cppgc

#endif  // INCLUDE_CPPGC_INTERNAL_HEAP_HANDLE_H_
