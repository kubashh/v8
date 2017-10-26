// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "src/base/macros.h"
#include "src/base/platform/platform-posix-time.h"
#include "src/base/platform/platform-posix.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"

namespace v8 {
namespace base {

// static
TimezoneCache* OS::CreateTimezoneCache() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return nullptr;
}

// static
int OS::ActivationFrameAlignment() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return 0;
}

// static
intptr_t OS::CommitPageSize() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return 0;
}

// static
void* OS::Allocate(const size_t requested, size_t* allocated,
                   OS::MemoryPermission access, void* hint) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return nullptr;
}

// static
void OS::Free(void* address, const size_t size) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

// static
void OS::SetReadAndExecutable(void* address, const size_t size) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

// static
void OS::Guard(void* address, size_t size) {
  CHECK_EQ(ZX_OK, zx_vmar_protect(zx_vmar_root_self(),
                                  reinterpret_cast<uintptr_t>(address), size,
                                  0 /*no permissions*/));
}

// static
void OS::SetReadAndWritable(void* address, const size_t size, bool commit) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

// static
void* OS::ReserveRegion(size_t size, void* hint) {
  zx_handle_t vmo;
  if (zx_vmo_create(size, 0, &vmo) != ZX_OK) return nullptr;
  uintptr_t result;
  zx_status_t status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size,
                                   0 /*no permissions*/, &result);
  zx_handle_close(vmo);
  if (status != ZX_OK) return nullptr;
  return reinterpret_cast<void*>(result);
}

// static
void* OS::ReserveAlignedRegion(size_t size, size_t alignment, void* hint,
                               size_t* allocated) {
  DCHECK_EQ(alignment % OS::AllocateAlignment(), 0);
  hint = AlignedAddress(hint, alignment);
  size_t request_size =
      RoundUp(size + alignment, static_cast<intptr_t>(OS::AllocateAlignment()));

  zx_handle_t vmo;
  if (zx_vmo_create(request_size, 0, &vmo) != ZX_OK) {
    *allocated = 0;
    return nullptr;
  }
  static const char kVirtualMemoryName[] = "v8-virtualmem";
  zx_object_set_property(vmo, ZX_PROP_NAME, kVirtualMemoryName,
                         strlen(kVirtualMemoryName));
  uintptr_t reservation;
  zx_status_t status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, request_size,
                                   0 /*no permissions*/, &reservation);
  // Either the vmo is now referenced by the vmar, or we failed and are bailing,
  // so close the vmo either way.
  zx_handle_close(vmo);
  if (status != ZX_OK) {
    *allocated = 0;
    return nullptr;
  }

  uint8_t* base = reinterpret_cast<uint8_t*>(reservation);
  uint8_t* aligned_base = RoundUp(base, alignment);
  DCHECK_LE(base, aligned_base);

  // Unmap extra memory reserved before and after the desired block.
  if (aligned_base != base) {
    size_t prefix_size = static_cast<size_t>(aligned_base - base);
    zx_vmar_unmap(zx_vmar_root_self(), reinterpret_cast<uintptr_t>(base),
                  prefix_size);
    request_size -= prefix_size;
  }

  size_t aligned_size = RoundUp(size, OS::AllocateAlignment());
  DCHECK_LE(aligned_size, request_size);

  if (aligned_size != request_size) {
    size_t suffix_size = request_size - aligned_size;
    zx_vmar_unmap(zx_vmar_root_self(),
                  reinterpret_cast<uintptr_t>(aligned_base + aligned_size),
                  suffix_size);
    request_size -= suffix_size;
  }

  DCHECK(aligned_size == request_size);

  *allocated = aligned_size;
  return static_cast<void*>(aligned_base);
}

// static
bool OS::CommitRegion(void* address, size_t size, bool is_executable) {
  uint32_t prot = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE |
                  (is_executable ? ZX_VM_FLAG_PERM_EXECUTE : 0);
  return zx_vmar_protect(zx_vmar_root_self(),
                         reinterpret_cast<uintptr_t>(address), size,
                         prot) == ZX_OK;
}

// static
bool OS::UncommitRegion(void* address, size_t size) {
  return zx_vmar_protect(zx_vmar_root_self(),
                         reinterpret_cast<uintptr_t>(address), size,
                         0 /*no permissions*/) == ZX_OK;
}

// static
bool OS::ReleaseRegion(void* address, size_t size) {
  return zx_vmar_unmap(zx_vmar_root_self(),
                       reinterpret_cast<uintptr_t>(address), size) == ZX_OK;
}

// static
bool OS::ReleasePartialRegion(void* address, size_t size) {
  return zx_vmar_unmap(zx_vmar_root_self(),
                       reinterpret_cast<uintptr_t>(address), size) == ZX_OK;
}

// static
bool OS::HasLazyCommits() {
  // TODO(scottmg): Port, https://crbug.com/731217.
  return false;
}

// static
void OS::Initialize(int64_t random_seed, bool hard_abort,
                    const char* const gc_fake_mmap) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

// static
const char* OS::GetGCFakeMMapFile() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return nullptr;
}

// static
void* OS::GetRandomMmapAddr() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return nullptr;
}

// static
size_t OS::AllocateAlignment() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return 0;
}

// static
void OS::Sleep(TimeDelta interval) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

// static
void OS::Abort() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

// static
void OS::DebugBreak() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

// static
std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return std::vector<SharedLibraryAddress>();
}

// static
void OS::SignalCodeMovingGC() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

// static
OS::MemoryMappedFile* OS::MemoryMappedFile::open(const char* name) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return nullptr;
}

// static
OS::MemoryMappedFile* OS::MemoryMappedFile::create(const char* name,
                                                   size_t size, void* initial) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return nullptr;
}

// static
int OS::GetCurrentProcessId() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return 0;
}

// static
int OS::GetCurrentThreadId() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return 0;
}

// static
int OS::GetUserTime(uint32_t* secs, uint32_t* usecs) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return 0;
}

// static
double OS::TimeCurrentMillis() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return 0.0;
}

// static
int OS::GetLastError() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return 0;
}

// static
FILE* OS::FOpen(const char* path, const char* mode) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return nullptr;
}

// static
bool OS::Remove(const char* path) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return false;
}

// static
char OS::DirectorySeparator() { return '/'; }

// static
bool OS::isDirectorySeparator(const char ch) {
  return ch == DirectorySeparator();
}

// static
FILE* OS::OpenTemporaryFile() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return nullptr;
}

// static
const char* const OS::LogFileOpenMode = "w";

// static
void OS::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrint(format, args);
  va_end(args);
}

// static
void OS::VPrint(const char* format, va_list args) { vprintf(format, args); }

// static
void OS::FPrint(FILE* out, const char* format, ...) {
  va_list args;
  va_start(args, format);
  VFPrint(out, format, args);
  va_end(args);
}

// static
void OS::VFPrint(FILE* out, const char* format, va_list args) {
  vfprintf(out, format, args);
}

// static
void OS::PrintError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrintError(format, args);
  va_end(args);
}

// static
void OS::VPrintError(const char* format, va_list args) {
  vfprintf(stderr, format, args);
}

// static
int OS::SNPrintF(char* str, int length, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int result = VSNPrintF(str, length, format, args);
  va_end(args);
  return result;
}

// static
int OS::VSNPrintF(char* str, int length, const char* format, va_list args) {
  int n = vsnprintf(str, length, format, args);
  if (n < 0 || n >= length) {
    // If the length is zero, the assignment fails.
    if (length > 0) str[length - 1] = '\0';
    return -1;
  } else {
    return n;
  }
}

// static
char* OS::StrChr(char* str, int c) { return strchr(str, c); }

// static
void OS::StrNCpy(char* dest, int length, const char* src, size_t n) {
  strncpy(dest, src, n);
}

Thread::Thread(const Options& options)
    : data_(nullptr),
      stack_size_(options.stack_size()),
      start_semaphore_(nullptr) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

Thread::~Thread() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

void Thread::set_name(const char* name) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

void Thread::Start() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

void Thread::Join() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

// static
Thread::LocalStorageKey Thread::CreateThreadLocalKey() {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return 0;
}

// static
void Thread::DeleteThreadLocalKey(LocalStorageKey key) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

// static
void* Thread::GetThreadLocal(LocalStorageKey key) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
  return nullptr;
}

// static
void Thread::SetThreadLocal(LocalStorageKey key, void* value) {
  UNREACHABLE();  // TODO(scottmg): Port, https://crbug.com/731217.
}

}  // namespace base
}  // namespace v8
