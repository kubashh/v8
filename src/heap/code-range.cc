// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/code-range.h"

#include "src/base/lazy-instance.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/heap/heap-inl.h"

namespace v8 {
namespace internal {

static base::LazyInstance<CodeRangeAddressHint>::type code_range_address_hint =
    LAZY_INSTANCE_INITIALIZER;

namespace {
void FunctionInStaticBinaryForAddressHint() {}
}  // anonymous namespace

Address CodeRangeAddressHint::GetAddressHint(size_t code_range_size) {
  base::MutexGuard guard(&mutex_);
  auto it = recently_freed_.find(code_range_size);
  if (it == recently_freed_.end() || it->second.empty()) {
    return FUNCTION_ADDR(&FunctionInStaticBinaryForAddressHint);
  }
  Address result = it->second.back();
  it->second.pop_back();
  return result;
}

void CodeRangeAddressHint::NotifyFreedCodeRange(Address code_range_start,
                                                size_t code_range_size) {
  base::MutexGuard guard(&mutex_);
  recently_freed_[code_range_size].push_back(code_range_start);
}

CodeRange::CodeRange() = default;

CodeRange::~CodeRange() { Free(); }

bool CodeRange::InitReservation(v8::PageAllocator* page_allocator,
                                size_t requested) {
  DCHECK_NE(requested, 0);
  DCHECK_NULL(code_page_allocator_.get());

  if (requested <= kMinimumCodeRangeSize) {
    requested = kMinimumCodeRangeSize;
  }

  const size_t reserved_area =
      kReservedCodeRangePages * MemoryAllocator::GetCommitPageSize();
  if (requested < (kMaximalCodeRangeSize - reserved_area)) {
    requested += RoundUp(reserved_area, MemoryChunk::kPageSize);
    // Fullfilling both reserved pages requirement and huge code area
    // alignments is not supported (requires re-implementation).
    DCHECK_LE(kMinExpectedOSPageSize, page_allocator->AllocatePageSize());
  }
  DCHECK_IMPLIES(kPlatformRequiresCodeRange,
                 requested <= kMaximalCodeRangeSize);

  Address hint =
      RoundDown(code_range_address_hint.Pointer()->GetAddressHint(requested),
                page_allocator->AllocatePageSize());
  VirtualMemory reservation(
      page_allocator, requested, reinterpret_cast<void*>(hint),
      std::max(kMinExpectedOSPageSize, page_allocator->AllocatePageSize()));
  if (!reservation.IsReserved()) {
    return false;
  }
  code_region_ = reservation.region();

  // We are sure that we have mapped a block of requested addresses.
  DCHECK_GE(reservation.size(), requested);
  Address base = reservation.address();

  // On some platforms, specifically Win64, we need to reserve some pages at
  // the beginning of an executable space. See
  //   https://cs.chromium.org/chromium/src/components/crash/content/
  //     app/crashpad_win.cc?rcl=fd680447881449fba2edcf0589320e7253719212&l=204
  // for details.
  if (reserved_area > 0) {
    if (!reservation.SetPermissions(base, reserved_area,
                                    PageAllocator::kReadWrite)) {
      return false;
    }

    base += reserved_area;
  }
  Address aligned_base = RoundUp(base, MemoryChunk::kAlignment);
  size_t size =
      RoundDown(reservation.size() - (aligned_base - base) - reserved_area,
                MemoryChunk::kPageSize);
  DCHECK(IsAligned(aligned_base, kMinExpectedOSPageSize));

  code_reservation_ = std::move(reservation);
  code_page_allocator_ = std::make_unique<base::BoundedPageAllocator>(
      page_allocator, aligned_base, size,
      static_cast<size_t>(MemoryChunk::kAlignment));
  return true;
}

void CodeRange::InitReservationOrDie(v8::PageAllocator* page_allocator,
                                     size_t requested) {
  if (!InitReservation(page_allocator, requested)) {
    V8::FatalProcessOutOfMemory(
        nullptr, "Could not allocate virtual memory for CodeRange");
  }
}

void CodeRange::Free() {
  if (IsReserved()) {
    DCHECK(!code_region_.is_empty());
    code_range_address_hint.Pointer()->NotifyFreedCodeRange(
        code_region_.begin(), code_region_.size());
    code_region_ = base::AddressRegion();
    code_page_allocator_.reset();
    code_reservation_.Free();
  }
}

uint8_t* CodeRange::RemapEmbeddedBuiltins(Isolate* isolate,
                                          const uint8_t* embedded_blob_code,
                                          size_t embedded_blob_code_size) {
  CHECK_NE(code_region_.begin(), kNullAddress);
  CHECK(!code_region_.is_empty());

  if (embedded_blob_code_copy_) {
    DCHECK(code_region_.contains(
        reinterpret_cast<Address>(embedded_blob_code_copy_),
        embedded_blob_code_size));
    SLOW_DCHECK(memcmp(embedded_blob_code, embedded_blob_code_copy_,
                       embedded_blob_code_size) == 0);
    return embedded_blob_code_copy_;
  }

  const size_t kAllocatePageSize = code_page_allocator()->AllocatePageSize();
  size_t allocate_code_size =
      RoundUp(embedded_blob_code_size, kAllocatePageSize);

  // Allocate the re-embedded code blob in the end.
  void* hint = reinterpret_cast<void*>(code_region_.end() - allocate_code_size);

  void* embedded_blob_copy = code_page_allocator()->AllocatePages(
      hint, allocate_code_size, kAllocatePageSize, PageAllocator::kNoAccess);

  if (!embedded_blob_copy) {
    V8::FatalProcessOutOfMemory(
        isolate, "Can't allocate space for re-embedded builtins");
  }

  size_t code_size =
      RoundUp(embedded_blob_code_size, code_page_allocator()->CommitPageSize());

  if (!code_page_allocator()->SetPermissions(embedded_blob_copy, code_size,
                                             PageAllocator::kReadWrite)) {
    V8::FatalProcessOutOfMemory(isolate,
                                "Re-embedded builtins: set permissions");
  }
  memcpy(embedded_blob_copy, embedded_blob_code, embedded_blob_code_size);

  if (!code_page_allocator()->SetPermissions(embedded_blob_copy, code_size,
                                             PageAllocator::kReadExecute)) {
    V8::FatalProcessOutOfMemory(isolate,
                                "Re-embedded builtins: set permissions");
  }

  embedded_blob_code_copy_ = reinterpret_cast<uint8_t*>(embedded_blob_copy);
  return embedded_blob_code_copy_;
}

}  // namespace internal
}  // namespace v8
