// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_EXPLICIT_MANAGEMENT_H_
#define INCLUDE_CPPGC_EXPLICIT_MANAGEMENT_H_

#include <cstddef>

#include "cppgc/internal/logging.h"
#include "cppgc/type-traits.h"

namespace cppgc {
namespace internal {

V8_EXPORT bool FreeUnreferencedObject(const void*);

}  // namespace internal

namespace subtle {

/**
 * Immediately reclaims `object`. The destructor may not be invoked immediately
 * but only on next garbage collection.
 *
 * It is up to the embedder to guarantee that no other object holds a reference
 * to `object` after calling `TryFree()`. In case such a reference exists, it's
 * use results in a use-after-free.
 *
 * \param object Reference to an object that is of type `GarbageCollected` and
 *   should be immediately reclaimed.
 * \param returns whether the `object` was reclaimed by the garbage collector.
 */
template <typename T>
void FreeUnreferencedObject(const T* object) {
  static_assert(IsGarbageCollectedTypeV<T>,
                "Object must be of type GarbageCollected.");
  if (!object) return;
  return internal::FreeUnreferencedObject(object);
}

}  // namespace subtle
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_EXPLICIT_MANAGEMENT_H_
