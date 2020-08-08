// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_CAGED_HEAP_ALLOCATOR_H_
#define V8_HEAP_CPPGC_CAGED_HEAP_ALLOCATOR_H_

#include "include/v8-platform.h"

namespace cppgc {
namespace internal {

class NormalPageAllocator : public v8::PageAllocator {
 public:
 private:
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_CAGED_HEAP_ALLOCATOR_H_
