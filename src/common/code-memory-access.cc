// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/code-memory-access.h"

namespace v8 {
namespace internal {

#if V8_HAS_PTHREAD_JIT_WRITE_PROTECT || defined(DEBUG)

thread_local int CodeMemoryWriteScope::code_space_write_nesting_level_ = 0;

#endif  // V8_HAS_PTHREAD_JIT_WRITE_PROTECT || defined(DEBUG)

}  // namespace internal
}  // namespace v8
