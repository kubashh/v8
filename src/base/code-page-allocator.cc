// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/code-page-allocator.h"

#include "src/base/platform/platform.h"
#include "src/base/platform/wrappers.h"

namespace v8 {
namespace base {

CodePageAllocator::CodePageAllocator()
    : allocate_page_size_(base::OS::AllocatePageSize()),
      commit_page_size_(base::OS::CommitPageSize()) {}

CodePageAllocator::CodePageAllocator(
    v8::PageAllocator* page_allocator,
    PlatformSharedMemoryHandle shared_memory_handle)
    : allocate_page_size_(base::OS::AllocatePageSize()),
      commit_page_size_(base::OS::CommitPageSize()),
      page_allocator_(page_allocator),
      shared_memory_handle_(shared_memory_handle) {
  CHECK_NE(shared_memory_handle_, kInvalidSharedMemoryHandle);
}

CodePageAllocator::~CodePageAllocator() {
  if (shared_memory_handle_ == kInvalidSharedMemoryHandle) return;
}

void* CodePageAllocator::AllocatePages(void* hint, size_t size,
                                       size_t alignment,
                                       PageAllocator::Permission access) {
#if !V8_HAS_PTHREAD_JIT_WRITE_PROTECT
  // kNoAccessWillJitLater is only used on Apple Silicon. Map it to regular
  // kNoAccess on other platforms, so code doesn't have to handle both enum
  // values.
  if (access == PageAllocator::kNoAccessWillJitLater) {
    access = PageAllocator::kNoAccess;
  }
#endif
  offset_ = 0;
  return base::OS::AllocateShared(
      hint, size, alignment, static_cast<base::OS::MemoryPermission>(access),
      shared_memory_handle_, &offset_);
}

bool CodePageAllocator::FreePages(void* address, size_t size) {
  return page_allocator_->FreePages(address, size);
}

bool CodePageAllocator::ReleasePages(void* address, size_t size,
                                     size_t new_size) {
  return page_allocator_->ReleasePages(address, size, new_size);
}

bool CodePageAllocator::SetPermissions(void* address, size_t size,
                                       PageAllocator::Permission access) {
  return page_allocator_->SetPermissions(address, size, access);
}

bool CodePageAllocator::RecommitPages(void* address, size_t size,
                                      PageAllocator::Permission access) {
  return page_allocator_->RecommitPages(address, size, access);
}

bool CodePageAllocator::DiscardSystemPages(void* address, size_t size) {
  return page_allocator_->DiscardSystemPages(address, size);
}

bool CodePageAllocator::DecommitPages(void* address, size_t size) {
  return page_allocator_->DecommitPages(address, size);
}

}  // namespace base
}  // namespace v8
