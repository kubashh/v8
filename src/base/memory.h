// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_MEMORY_H_
#define V8_BASE_MEMORY_H_

#include "src/base/macros.h"
#include "src/base/platform/wrappers.h"

namespace v8 {
namespace base {

using Address = uintptr_t;
using byte = uint8_t;

// Memory provides an interface to 'raw' memory. It encapsulates the casts
// that typically are needed when incompatible pointer types are used.
// Note that this class currently relies on undefined behaviour. There is a
// proposal (http://wg21.link/p0593r2) to make it defined behaviour though.
template <class T>
inline T& Memory(Address addr) {
  DCHECK(IsAligned(addr, alignof(T)));
  return *reinterpret_cast<T*>(addr);
}
template <class T>
inline T& Memory(byte* addr) {
  return Memory<T>(reinterpret_cast<Address>(addr));
}

template <typename V>
static inline V ReadUnalignedValue(Address p) {
  ASSERT_TRIVIALLY_COPYABLE(V);
  V r;
  base::Memcpy(&r, reinterpret_cast<void*>(p), sizeof(V));
  return r;
}

template <typename V>
static inline void WriteUnalignedValue(Address p, V value) {
  ASSERT_TRIVIALLY_COPYABLE(V);
  base::Memcpy(reinterpret_cast<void*>(p), &value, sizeof(V));
}

template <typename V>
static inline V ReadByteReversedValue(Address p) {
  V ret{};
  const byte* src = reinterpret_cast<const byte*>(p);
  byte* dst = reinterpret_cast<byte*>(&ret);
  for (size_t i = 0; i < sizeof(V); i++) {
    dst[i] = src[sizeof(V) - i - 1];
  }
  return ret;
}

template <typename V>
static inline void WriteByteReversedValue(Address p, V value) {
  const byte* src = reinterpret_cast<const byte*>(&value);
  byte* dst = reinterpret_cast<byte*>(p);
  for (size_t i = 0; i < sizeof(V); i++) {
    dst[i] = src[sizeof(V) - i - 1];
  }
}

template <typename V>
static inline V ReadLittleEndianValue(Address p) {
#if defined(V8_HOST_LITTLE_ENDIAN)
  return ReadUnalignedValue<V>(p);
#elif defined(V8_HOST_BIG_ENDIAN)
  return ReadByteReversedValue<V>(p);
#endif
}

template <typename V>
static inline void WriteLittleEndianValue(Address p, V value) {
#if defined(V8_HOST_LITTLE_ENDIAN)
  WriteUnalignedValue<V>(p, value);
#elif defined(V8_HOST_BIG_ENDIAN)
  WriteByteReversedValue<V>(p, value);
#endif
}

template <typename V>
static inline V ReadLittleEndianValue(V* p) {
  return ReadLittleEndianValue<V>(reinterpret_cast<Address>(p));
}

template <typename V>
static inline void WriteLittleEndianValue(V* p, V value) {
  static_assert(
      !std::is_array<V>::value,
      "Passing an array decays to pointer, causing unexpected results.");
  WriteLittleEndianValue<V>(reinterpret_cast<Address>(p), value);
}

template <typename V>
static inline V ReadBigEndianValue(Address p) {
#if defined(V8_HOST_BIG_ENDIAN)
  return ReadUnalignedValue<V>(p);
#elif defined(V8_HOST_LITTLE_ENDIAN)
  return ReadByteReversedValue<V>(p);
#endif
}

template <typename V>
static inline void WriteBigEndianValue(Address p, V value) {
#if defined(V8_HOST_BIG_ENDIAN)
  WriteUnalignedValue<V>(p, value);
#elif defined(V8_HOST_LITTLE_ENDIAN)
  WriteByteReversedValue<V>(p, value);
#endif
}

template <typename V>
static inline V ReadBigEndianValue(const V* p) {
  return ReadBigEndianValue<V>(reinterpret_cast<Address>(p));
}

template <typename V>
static inline void WriteBigEndianValue(V* p, V value) {
  static_assert(
      !std::is_array<V>::value,
      "Passing an array decays to pointer, causing unexpected results.");
  WriteBigEndianValue<V>(reinterpret_cast<Address>(p), value);
}

template <typename V>
static inline V ReadTargetEndianValue(Address p) {
#if defined(V8_TARGET_LITTLE_ENDIAN)
  return ReadLittleEndianValue<V>(p);
#elif defined(V8_TARGET_BIG_ENDIAN)
  return ReadBigEndianValue<V>(p);
#endif
}

template <typename V>
static inline void WriteTargetEndianValue(Address p, V value) {
#if defined(V8_TARGET_LITTLE_ENDIAN)
  WriteLittleEndianValue<V>(p, value);
#elif defined(V8_TARGET_BIG_ENDIAN)
  WriteBigEndianValue<V>(p, value);
#endif
}

template <typename V>
static inline V ReadTargetEndianValue(const V* p) {
  return ReadTargetEndianValue<V>(reinterpret_cast<Address>(p));
}

template <typename V>
static inline void WriteTargetEndianValue(V* p, V value) {
  static_assert(
      !std::is_array<V>::value,
      "Passing an array decays to pointer, causing unexpected results.");
  WriteTargetEndianValue<V>(reinterpret_cast<Address>(p), value);
}

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_MEMORY_H_
