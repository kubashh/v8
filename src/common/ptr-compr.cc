// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/ptr-compr-inl.h"

namespace v8::internal {

#ifdef V8_COMPRESS_POINTERS

thread_local uintptr_t V8HeapCompressionScheme::base_ = kNullAddress;

#ifdef V8_EXTERNAL_CODE_SPACE
thread_local uintptr_t ExternalCodeCompressionScheme::base_ = kNullAddress;
#endif  // V8_EXTERNAL_CODE_SPACE

// static
Address V8HeapCompressionScheme::base_non_inlined() { return base_; }

// static
void V8HeapCompressionScheme::set_base_non_inlined(Address base) {
  base_ = base;
}

#ifdef V8_EXTERNAL_CODE_SPACE
// static
Address ExternalCodeCompressionScheme::base_non_inlined() { return base_; }

// static
void ExternalCodeCompressionScheme::set_base_non_inlined(Address base) {
  base_ = base;
}
#endif  // V8_EXTERNAL_CODE_SPACE

#endif  // V8_COMPRESS_POINTERS

}  // namespace v8::internal
