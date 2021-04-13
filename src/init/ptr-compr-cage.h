// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INIT_PTR_COMPR_CAGE_H_
#define V8_INIT_PTR_COMPR_CAGE_H_

#include <memory>

#include "src/base/bounded-page-allocator.h"
#include "src/base/page-allocator.h"
#include "src/common/globals.h"
#include "src/heap/code-range.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE PtrComprCage final {
 public:
  PtrComprCage();
  ~PtrComprCage();

  PtrComprCage(const PtrComprCage&) = delete;
  PtrComprCage& operator=(PtrComprCage&) = delete;

  PtrComprCage(PtrComprCage&& other) V8_NOEXCEPT { *this = std::move(other); }

  PtrComprCage& operator=(PtrComprCage&& other) V8_NOEXCEPT {
    base_ = other.base_;
    other.base_ = kNullAddress;
    page_allocator_ = std::move(other.page_allocator_);
    reservation_ = std::move(other.reservation_);
    code_range_ = std::move(other.code_range_);
    return *this;
  }

  Address base() const { return base_; }

  base::BoundedPageAllocator* page_allocator() const {
    return page_allocator_.get();
  }

  const VirtualMemory* reservation() const { return &reservation_; }

  CodeRange* code_range() { return &code_range_; }
  const CodeRange* code_range() const { return &code_range_; }

  bool IsReserved() const {
    DCHECK_EQ(base_ != kNullAddress, reservation_.IsReserved());
    return base_ != kNullAddress;
  }

  bool InitReservation();
  void InitReservationOrDie();

  bool InitCodeRange(size_t requested_code_range_size);
  void InitCodeRangeOrDie(size_t requested_code_range_size);

  void Free();

  static void InitializeOncePerProcess();
  static PtrComprCage* GetProcessWideCage();

 private:
  friend class IsolateAllocator;

  static bool RequiresProcessWideCodeRange();

  Address base_ = kNullAddress;
  std::unique_ptr<base::BoundedPageAllocator> page_allocator_;
  VirtualMemory reservation_;
#ifndef V8_ENABLE_THIRD_PARTY_HEAP
  CodeRange code_range_;
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // V8_INIT_PTR_COMPR_CAGE_H_
