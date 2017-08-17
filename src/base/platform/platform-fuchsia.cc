// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mman.h>

#include "src/base/macros.h"
#include "src/base/platform/platform-posix-time.h"
#include "src/base/platform/platform-posix.h"
#include "src/base/platform/platform.h"

namespace v8 {
namespace base {

TimezoneCache* OS::CreateTimezoneCache() {
  return new PosixDefaultTimezoneCache();
}

void* OS::Allocate(const size_t requested, size_t* allocated,
                   OS::MemoryPermission access, void* hint) {
  const size_t msize = RoundUp(requested, AllocateAlignment());
  int prot = GetProtectionFromMemoryPermission(access);
  void* mbase = mmap(hint, msize, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mbase == MAP_FAILED) return nullptr;
  *allocated = msize;
  return mbase;
}

std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  CHECK(false);  // TODO(scottmg): Port, https://crbug.com/731217.
  return std::vector<SharedLibraryAddress>();
}

void OS::SignalCodeMovingGC() {
  CHECK(false);  // TODO(scottmg): Port, https://crbug.com/731217.
}

VirtualMemory::VirtualMemory() : address_(nullptr), size_(0) {}

VirtualMemory::VirtualMemory(size_t size, void* hint)
    : address_(ReserveRegion(size, hint)), size_(size) {}

VirtualMemory::VirtualMemory(size_t size, size_t alignment, void* hint)
    : address_(nullptr), size_(0) {
  DCHECK((alignment % OS::AllocateAlignment()) == 0);
  hint = AlignedAddress(hint, alignment);
  size_t request_size =
      RoundUp(size + alignment, static_cast<intptr_t>(OS::AllocateAlignment()));
  void* reservation = mmap(hint, request_size, PROT_NONE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (reservation == MAP_FAILED) return;

  uint8_t* base = static_cast<uint8_t*>(reservation);
  uint8_t* aligned_base = RoundUp(base, alignment);
  DCHECK_LE(base, aligned_base);

  // Unmap extra memory reserved before and after the desired block.
  if (aligned_base != base) {
    size_t prefix_size = static_cast<size_t>(aligned_base - base);
    OS::Free(base, prefix_size);
    request_size -= prefix_size;
  }

  size_t aligned_size = RoundUp(size, OS::AllocateAlignment());
  DCHECK_LE(aligned_size, request_size);

  if (aligned_size != request_size) {
    size_t suffix_size = request_size - aligned_size;
    OS::Free(aligned_base + aligned_size, suffix_size);
    request_size -= suffix_size;
  }

  DCHECK(aligned_size == request_size);

  address_ = static_cast<void*>(aligned_base);
  size_ = aligned_size;
}

VirtualMemory::~VirtualMemory() {
  if (IsReserved()) {
    bool result = ReleaseRegion(address(), size());
    DCHECK(result);
    USE(result);
  }
}

void VirtualMemory::Reset() {
  address_ = nullptr;
  size_ = 0;
}

bool VirtualMemory::Commit(void* address, size_t size, bool is_executable) {
  CHECK(InVM(address, size));
  return CommitRegion(address, size, is_executable);
}

bool VirtualMemory::Uncommit(void* address, size_t size) {
  return UncommitRegion(address, size);
}

bool VirtualMemory::Guard(void* address) {
  OS::Guard(address, OS::CommitPageSize());
  return true;
}

// static
void* VirtualMemory::ReserveRegion(size_t size, void* hint) {
  void* result = mmap(hint, size, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

  if (result == MAP_FAILED) return nullptr;

  return result;
}

// static
bool VirtualMemory::CommitRegion(void* base, size_t size, bool is_executable) {
  int prot = PROT_READ | PROT_WRITE | (is_executable ? PROT_EXEC : 0);
  return mprotect(base, size, prot) != -1;
}

// static
bool VirtualMemory::UncommitRegion(void* base, size_t size) {
  return mprotect(base, size, PROT_NONE) != -1;
}

// static
bool VirtualMemory::ReleasePartialRegion(void* base, size_t size,
                                         void* free_start, size_t free_size) {
  return munmap(free_start, free_size) == 0;
}

// static
bool VirtualMemory::ReleaseRegion(void* base, size_t size) {
  return munmap(base, size) == 0;
}

// static
bool VirtualMemory::HasLazyCommits() {
  // TODO(scottmg): Port, https://crbug.com/731217.
  return false;
}

}  // namespace base
}  // namespace v8
