// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/ptr-compr-inl.h"

namespace v8::internal {

#ifdef V8_COMPRESS_POINTERS

#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
#define THREAD_LOCAL_IF_MULTICAGE
#else
#define THREAD_LOCAL_IF_MULTICAGE thread_local
#endif  // V8_COMPRESS_POINTERS_IN_SHARED_CAGE

// static
template <typename Cage>
Address V8HeapCompressionSchemeImpl<Cage>::base_non_inlined() {
  return base_;
}

// static
template <typename Cage>
void V8HeapCompressionSchemeImpl<Cage>::set_base_non_inlined(Address base) {
  base_ = base;
}

#ifdef V8_EXTERNAL_CODE_SPACE

THREAD_LOCAL_IF_MULTICAGE uintptr_t ExternalCodeCompressionScheme::base_ =
    kNullAddress;

// static
Address ExternalCodeCompressionScheme::base_non_inlined() { return base_; }

// static
void ExternalCodeCompressionScheme::set_base_non_inlined(Address base) {
  base_ = base;
}
#endif  // V8_EXTERNAL_CODE_SPACE

// Explicitly instantiate the V8HeapCompressionScheme.
template class V8HeapCompressionSchemeImpl<MainCage>;

#ifdef V8_ENABLE_SANDBOX
// Explicitly instantiate the TrustedSpaceCompressionScheme.
template class V8HeapCompressionSchemeImpl<TrustedCage>;
#endif  // V8_ENABLE_SANDBOX

#undef THREAD_LOCAL_IF_MULTICAGE

#endif  // V8_COMPRESS_POINTERS

}  // namespace v8::internal
