// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CODE_RANGE_H_
#define V8_HEAP_CODE_RANGE_H_

#include <unordered_map>
#include <vector>

#include "src/base/bounded-page-allocator.h"
#include "src/base/page-allocator.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

// The process-wide singleton that keeps track of code range regions with the
// intention to reuse free code range regions as a workaround for CFG memory
// leaks (see crbug.com/870054).
class CodeRangeAddressHint {
 public:
  // Returns the most recently freed code range start address for the given
  // size. If there is no such entry, then a random address is returned.
  V8_EXPORT_PRIVATE Address GetAddressHint(size_t code_range_size);

  V8_EXPORT_PRIVATE void NotifyFreedCodeRange(Address code_range_start,
                                              size_t code_range_size);

 private:
  base::Mutex mutex_;
  // A map from code range size to an array of recently freed code range
  // addresses. There should be O(1) different code range sizes.
  // The length of each array is limited by the peak number of code ranges,
  // which should be also O(1).
  std::unordered_map<size_t, std::vector<Address>> recently_freed_;
};

class CodeRange final {
 public:
  CodeRange();
  V8_EXPORT_PRIVATE ~CodeRange();

  CodeRange(const CodeRange&) = delete;
  CodeRange& operator=(CodeRange&) = delete;

  CodeRange(CodeRange&& other) V8_NOEXCEPT { *this = std::move(other); }

  CodeRange& operator=(CodeRange&& other) V8_NOEXCEPT {
    code_page_allocator_ = std::move(other.code_page_allocator_);
    code_region_ = other.code_region_;
    other.code_region_ = base::AddressRegion();
    code_reservation_ = std::move(other.code_reservation_);
    return *this;
  }

  base::BoundedPageAllocator* code_page_allocator() const {
    return code_page_allocator_.get();
  }

  // A region of memory that may contain executable code including reserved
  // OS page with read-write access in the beginning.
  const base::AddressRegion& code_region() const {
    // |code_region_| >= |optional RW pages| + |code_page_allocator_|
    DCHECK_IMPLIES(!code_region_.is_empty(), code_page_allocator_);
    DCHECK_IMPLIES(!code_region_.is_empty(),
                   code_region_.contains(code_page_allocator_->begin(),
                                         code_page_allocator_->size()));
    return code_region_;
  }

  bool IsReserved() const { return code_reservation_.IsReserved(); }

  bool InitReservation(v8::PageAllocator* page_allocator, size_t requested);
  void InitReservationOrDie(v8::PageAllocator* page_allocator,
                            size_t requested);
  void Free();

  // Remap and copy the embedded builtins into this CodeRange. This method is
  // idempotent and only performs the copy once. This property is so that this
  // method can be used uniformly regardless of having a per-Isolate or a shared
  // pointer cage. Returns the address of the copy.
  //
  // The builtins code region will be freed with the code range at tear down.
  //
  // When ENABLE_SLOW_DCHECKS is on, the contents of the embedded_blob_code are
  // compared against the already copied version.
  uint8_t* RemapEmbeddedBuiltins(Isolate* isolate,
                                 const uint8_t* embedded_blob_code,
                                 size_t embedded_blob_code_size);

 private:
  // A part of the |code_reservation_| that may contain executable code
  // including reserved page with read-write access in the beginning.
  // See details below.
  base::AddressRegion code_region_;

  // This unique pointer owns the instance of bounded code allocator
  // that controls executable pages allocation. It does not control the
  // optionally existing page in the beginning of the |code_region_|.
  // So, summarizing all above, the following conditions hold:
  // 1) |code_reservation_| >= |code_region_|
  // 2) |code_region_| >= |optional RW pages| +
  // |code_page_allocator_|. 3) |code_reservation_| is
  // AllocatePageSize()-aligned 4) |code_page_allocator_| is
  // MemoryChunk::kAlignment-aligned 5) |code_region_| is
  // CommitPageSize()-aligned
  std::unique_ptr<base::BoundedPageAllocator> code_page_allocator_;

  // Used when short builtin calls are enabled, where embedded builtins are
  // copied into the CodeRange so calls can be nearer.
  uint8_t* embedded_blob_code_copy_ = nullptr;

  // The virtual space reserved for code on the V8 heap.
  VirtualMemory code_reservation_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CODE_RANGE_H_
