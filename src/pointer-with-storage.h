#ifndef POINTER_WITH_STORAGE_H
#define POINTER_WITH_STORAGE_H

#include <cstdint>

#include "include/v8config.h"
#include "src/base/logging.h"

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

#endif  // POINTER_WITH_STORAGE_H
