// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_TRUSTED_RANGE_H_
#define V8_HEAP_TRUSTED_RANGE_H_

#include "src/common/globals.h"
#include "src/utils/allocation.h"
#include "v8-internal.h"

namespace v8 {
namespace internal {

#ifdef V8_CODE_POINTER_SANDBOXING

// When the sandbox is enabled, the heap's trusted spaces are located outside
// of the sandbox so that an attacker cannot corrupt their contents. This
// special virtual memory cage hosts them. It also acts as a pointer
// compression cage inside of which compressed pointers can be used to
// reference objects.
class TrustedRange final : public VirtualMemoryCage {
 public:
  bool InitReservation(size_t requested);

  static TrustedRange* EnsureProcessWideTrustedRange(size_t requested_size);

  // If InitializeProcessWideCodeRangeOnce has been called, returns the
  // initialized TrustedRange. Otherwise returns a null pointer.
  V8_EXPORT_PRIVATE static TrustedRange* GetProcessWideTrustedRange();
};

#endif  // V8_CODE_POINTER_SANDBOXING

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_TRUSTED_RANGE_H_
