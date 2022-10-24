// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_CODE_PAGE_ALLOCATOR_H_
#define V8_BASE_CODE_PAGE_ALLOCATOR_H_

#include "include/v8-platform.h"
#include "src/base/base-export.h"
#include "src/base/compiler-specific.h"

namespace v8 {
namespace base {

class V8_BASE_EXPORT CodePageAllocator
    : public NON_EXPORTED_BASE(::v8::PageAllocator) {
 public:
  CodePageAllocator();
  CodePageAllocator(v8::PageAllocator* page_allocator,
                    PlatformSharedMemoryHandle shared_memory_handle);
  ~CodePageAllocator() override;

  size_t AllocatePageSize() override { return allocate_page_size_; }

  size_t CommitPageSize() override { return commit_page_size_; }

  void SetRandomMmapSeed(int64_t seed) override {
    page_allocator_->SetRandomMmapSeed(seed);
  }

  void* GetRandomMmapAddr() override {
    return page_allocator_->GetRandomMmapAddr();
  }

  void* AllocatePages(void* hint, size_t size, size_t alignment,
                      PageAllocator::Permission access) override;

  bool FreePages(void* address, size_t size) override;

  bool ReleasePages(void* address, size_t size, size_t new_size) override;

  bool SetPermissions(void* address, size_t size,
                      PageAllocator::Permission access) override;

  bool RecommitPages(void* address, size_t size,
                     PageAllocator::Permission access) override;

  bool DiscardSystemPages(void* address, size_t size) override;

  bool DecommitPages(void* address, size_t size) override;

  size_t offset() const { return offset_; }

 private:
  const size_t allocate_page_size_;
  const size_t commit_page_size_;
  v8::PageAllocator* page_allocator_ = nullptr;

  PlatformSharedMemoryHandle shared_memory_handle_ = kInvalidSharedMemoryHandle;
  size_t offset_ = 0;
};

}  // namespace base
}  // namespace v8
#endif  // V8_BASE_CODE_PAGE_ALLOCATOR_H_
