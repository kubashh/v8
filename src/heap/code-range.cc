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

// static
bool CodeRange::RequiresProcessWideRange() {
  return kPlatformRequiresCodeRange && !FLAG_jitless;
}

// static
void CodeRange::InitializeOncePerProcess() {
#if !defined(V8_ENABLE_THIRD_PARTY_HEAP) && \
    defined(V8_COMPRESS_POINTERS_IN_SHARED_CAGE)
  if (RequiresProcessWideRange()) {
    PtrComprCage* cage = PtrComprCage::GetProcessWideCage();
    CHECK(cage->IsReserved());
    // TODO(11460): Make the size configurable.
    GetProcessWideRange()->InitReservationOrDie(cage->page_allocator(),
                                                kMaximalCodeRangeSize);
  }
#endif
}

#if !defined(V8_ENABLE_THIRD_PARTY_HEAP) && \
    defined(V8_COMPRESS_POINTERS_IN_SHARED_CAGE)

namespace {
DEFINE_LAZY_LEAKY_OBJECT_GETTER(CodeRange, GetSharedProcessWideRange)
}  // anonymous namespace

// static
CodeRange* CodeRange::GetProcessWideRange() {
  return GetSharedProcessWideRange();
}

#endif  // V8_COMPRESS_POINTERS_IN_SHARED_CAGE

}  // namespace internal
}  // namespace v8
