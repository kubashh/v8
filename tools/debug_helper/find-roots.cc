// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug-helper.h"

namespace d = v8::debug_helper;

namespace v8_debug_helper_internal {

d::Roots FindRoots(d::MemoryAccessor memory_accessor,
                   d::TlsAccessor tls_accessor, d::GlobalFinder global_finder) {
  d::Roots result{0, 0, 0, 0};
  // TODO fetch "v8::internal::Isolate::isolate_key_" symbolically from caller,
  // then fetch the TLS data at the given offset to get isolate.
  // Then read data from isolate.heap_ to get the various heap roots.
  // Of course any of these steps could fail, but they are the ideal case for
  // live debugging or full dumps.
  // If pointer compression is enabled, then the isolate is a heap pointer so at
  // least we can decompress things.
  return result;
}

}  // namespace v8_debug_helper_internal

extern "C" {
V8_DEBUG_HELPER_EXPORT void _v8_debug_helper_FindRoots(
    d::MemoryAccessor memory_accessor, d::TlsAccessor tls_accessor,
    d::GlobalFinder global_finder, d::Roots* roots) {
  *roots = v8_debug_helper_internal::FindRoots(memory_accessor, tls_accessor,
                                               global_finder);
}
}
