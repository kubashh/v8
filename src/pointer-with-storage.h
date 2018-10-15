// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_POINTER_WITH_STORAGE_H_
#define V8_POINTER_WITH_STORAGE_H_

#include <cstdint>

#include "include/v8config.h"
#include "src/base/logging.h"

namespace v8 {
namespace internal {

// PointerWithStorageBits combines a PointerType an a small StorageType into
// one. The bits of the storage type get packed into the lower bits of the
// pointer that are free due to alignment. The user needs to specify how many
// bits are needed to store the StorageType, allowing Types that by default are
// larger to be stored.
//
// Example:
//   PointerWithStorageBits<int *, bool, 1> data_and_flag;
//
//   Here we store a bool that needs 1 bit of storage state into the lower bits
//   of int *, which points to some int data;

template <typename PointerType, typename StoredType, int NumStorageBits>
class PointerWithStorageBits {
  // We have log2(ptr alignment) kAvailBits free to use
  static constexpr int kAvailBits =
      alignof(PointerType) >= 8 ? 3 : alignof(PointerType) >= 4 ? 2 : 1;
  static_assert(kAvailBits >= NumStorageBits,
                "Ptr has no sufficient alignment for the selected amount of "
                "storage bits.");

  static constexpr uintptr_t kPointerMask =
      ~(uintptr_t)(((intptr_t)1 << kAvailBits) - 1);
  static constexpr uintptr_t kStorageMask = (uintptr_t)((1 << kAvailBits) - 1);

 public:
  V8_INLINE PointerType GetPointer() const {
    return reinterpret_cast<PointerType>(pointer_ & kPointerMask);
  }

  V8_INLINE PointerType operator->() { return GetPointer(); }

  V8_INLINE StoredType GetStorage() const {
    return static_cast<StoredType>(pointer_ & kStorageMask);
  }

  V8_INLINE void SetPointer(PointerType newptr) {
    DCHECK(reinterpret_cast<PointerType>(reinterpret_cast<intptr_t>(newptr) &
                                         kPointerMask) == newptr);
    pointer_ = reinterpret_cast<intptr_t>(newptr) | (pointer_ & ~kPointerMask);
    DCHECK(GetPointer() == newptr);
  }

  V8_INLINE void SetStorage(StoredType newstore) {
    DCHECK((newstore & kStorageMask) == newstore);
    pointer_ = (pointer_ & ~kStorageMask) | static_cast<intptr_t>(newstore);
    DCHECK(GetStorage() == newstore);
  }

 private:
  intptr_t pointer_ = 0;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_POINTER_WITH_STORAGE_H_
