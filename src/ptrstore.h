#ifndef PTR_STORE_H
#define PTR_STORE_H

#include <cstdint>

#include "src/base/logging.h"

template <typename PtrT, int StorageBits>
struct StoragePtr {
  // We have log2(ptr alignment) avail_bits free to use
  static constexpr int avail_bits =
      alignof(PtrT) >= 8 ? 3 : alignof(PtrT) >= 4 ? 2 : 1;

  static_assert(avail_bits >= StorageBits,
                "Ptr has no sufficient alignment for the selected amount of "
                "storage bits.");
  static constexpr int usedbits = StorageBits;

  static constexpr uintptr_t mask_store_bits =
      ~(uintptr_t)(((intptr_t)1 << avail_bits) - 1);
  static constexpr uintptr_t mask_ptr_bits = (uintptr_t)((1 << avail_bits) - 1);

  inline PtrT getPtr() const {
    return reinterpret_cast<PtrT>(ptr & mask_store_bits);
  }

  inline int getStorage() const { return ptr & mask_ptr_bits; }

  inline void setPtr(PtrT newptr) {
    ptr = reinterpret_cast<intptr_t>(newptr) | (ptr & ~mask_store_bits);
  }

  inline void setStorage(int newstore) {
    DCHECK((newstore & mask_ptr_bits) == newstore);
    ptr = (ptr & ~mask_ptr_bits) | static_cast<intptr_t>(newstore);
  }

 private:
  intptr_t ptr = 0;
};

#endif  // PTR_STORE_H
