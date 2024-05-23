// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_SIMD128_H_
#define V8_COMMON_SIMD128_H_

#include <cstdint>

#include "src/base/memory.h"
#include "src/common/globals.h"

namespace v8::internal {

// TODO(mliedtke): Rename the C++ types to int64x, ...
#define FOREACH_SIMD_TYPE(V)  \
  V(double, float2, f64x2, 2) \
  V(float, float4, f32x4, 4)  \
  V(int64_t, int2, i64x2, 2)  \
  V(int32_t, int4, i32x4, 4)  \
  V(int16_t, int8, i16x8, 8)  \
  V(int8_t, int16, i8x16, 16)

#define DEFINE_SIMD_TYPE(cType, sType, name, kSize) \
  struct sType {                                    \
    cType val[kSize];                               \
  };
FOREACH_SIMD_TYPE(DEFINE_SIMD_TYPE)
#undef DEFINE_SIMD_TYPE

class alignas(double) Simd128 {
 public:
  Simd128() = default;

#define DEFINE_SIMD_TYPE_SPECIFIC_METHODS(cType, sType, name, size)          \
  explicit Simd128(sType val) {                                              \
    base::WriteUnalignedValue<sType>(reinterpret_cast<Address>(val_), val);  \
  }                                                                          \
  sType to_##name() const {                                                  \
    return base::ReadUnalignedValue<sType>(reinterpret_cast<Address>(val_)); \
  }
  FOREACH_SIMD_TYPE(DEFINE_SIMD_TYPE_SPECIFIC_METHODS)
#undef DEFINE_SIMD_TYPE_SPECIFIC_METHODS

  explicit Simd128(uint8_t* bytes) {
    memcpy(static_cast<void*>(val_), reinterpret_cast<void*>(bytes),
           v8::internal::kSimd128Size);
  }

  bool operator==(const Simd128& other) const noexcept {
    return memcmp(val_, other.val_, sizeof val_) == 0;
  }

  const uint8_t* bytes() { return val_; }

  template <typename T>
  inline T to() const;

#ifdef V8_ENABLE_DRUMBRAKE
  struct hash {
    size_t operator()(const Simd128& s128) const { return s128.hash_value(); }
  };
  size_t hash_value() const {
    static_assert(sizeof(size_t) == sizeof(uint64_t));
    const int2 s = to_i64x2();
    return s.val[0] ^ s.val[1];
  }
#endif  // V8_ENABLE_DRUMBRAKE

 private:
  uint8_t val_[16] = {0};
};

#define DECLARE_CAST(cType, sType, name, size) \
  template <>                                  \
  inline sType Simd128::to() const {           \
    return to_##name();                        \
  }
FOREACH_SIMD_TYPE(DECLARE_CAST)
#undef DECLARE_CAST

}  // namespace v8::internal

#endif  // V8_COMMON_SIMD128_H_
