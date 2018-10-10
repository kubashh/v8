#ifndef PTR_STORE_H
#define PTR_STORE_H

#include <cstdint>

#include "src/base/logging.h"

template <typename PtrT, int StorageBits>
struct StoragePtr {
  static_assert(alignof(PtrT) == 8, "Ptr needs to be 8 byte aligned");
  static constexpr int bits = 3;
  static constexpr int usedbits = StorageBits;

  static constexpr uintptr_t mask_store_bits =
      ~(uintptr_t)(((intptr_t)1 << bits) - 1);
  static constexpr uintptr_t mask_ptr_bits = (uintptr_t)((1 << bits) - 1);

  inline PtrT getPtr() const {
    return reinterpret_cast<PtrT>(ptr & mask_store_bits);
  }

  inline int getStorage() const {
    return ptr & mask_ptr_bits;
  }

  inline void setPtr(PtrT newptr) {
    ptr = reinterpret_cast<intptr_t>(newptr) | (ptr & ~mask_store_bits);
  }

  inline void setStorage(int newstore) {
    DCHECK((newstore & mask_ptr_bits) == newstore );
    ptr = (ptr & ~mask_ptr_bits) | static_cast<intptr_t>(newstore) ;
  }

 private:
  intptr_t ptr = 0;
};

#endif // PTR_STORE_H
