#ifndef PTR_STORE_H
#define PTR_STORE_H

#include <cstdint>

#include "src/base/logging.h"

template <typename PointerType, int NumStorageBits>
struct PointerWithStorageBits {
  // We have log2(ptr alignment) kAvailBits free to use
  static constexpr int kAvailBits =
      alignof(PointerType) >= 8 ? 3 : alignof(PointerType) >= 4 ? 2 : 1;

  static_assert(kAvailBits >= NumStorageBits,
                "Ptr has no sufficient alignment for the selected amount of "
                "storage bits.");
  static constexpr int usedbits = NumStorageBits;

  static constexpr uintptr_t kMaskOutStorageBits =
      ~(uintptr_t)(((intptr_t)1 << kAvailBits) - 1);
  static constexpr uintptr_t kMaskOutPointerBits =
      (uintptr_t)((1 << kAvailBits) - 1);

  inline PointerType GetPointer() const {
    return reinterpret_cast<PointerType>(pointer_ & kMaskOutStorageBits);
  }

  inline int GetStorage() const { return pointer_ & kMaskOutPointerBits; }

  inline void SetPointer(PointerType newptr) {
    pointer_ =
        reinterpret_cast<intptr_t>(newptr) | (pointer_ & ~kMaskOutStorageBits);
  }

  inline void SetStorage(int newstore) {
    DCHECK((newstore & kMaskOutPointerBits) == newstore);
    pointer_ =
        (pointer_ & ~kMaskOutPointerBits) | static_cast<intptr_t>(newstore);
  }

 private:
  intptr_t pointer_ = 0;
};

#endif  // PTR_STORE_H
