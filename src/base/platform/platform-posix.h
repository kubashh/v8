// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PLATFORM_PLATFORM_POSIX_H_
#define V8_BASE_PLATFORM_PLATFORM_POSIX_H_

#include "include/v8config.h"
#include "src/base/platform/platform.h"
#include "src/base/timezone-cache.h"

namespace v8 {
namespace base {

void PosixInitializeCommon(bool hard_abort, const char* const gc_fake_mmap);

class PosixTimezoneCache : public TimezoneCache {
 public:
  double DaylightSavingsOffset(double time_ms) override;
  void Clear(TimeZoneDetection) override {}
  ~PosixTimezoneCache() override = default;

 protected:
  static const int msPerSecond = 1000;
};

#if !V8_OS_FUCHSIA
int GetProtectionFromMemoryPermission(OS::MemoryPermission access);
#endif

#if V8_HAS_PKU_JIT_WRITE_PROTECT
class V8_BASE_EXPORT PKU {
 public:
  PKU();

  bool SetPermissionsAndMemoryProtectionKey(void* address, size_t size,
                                            OS::MemoryPermission permissions);

  void SetWriteableForMemoryProtectionKey(bool writeable);

  int GetPermissionsProtectionKey();

  OS::MemoryProtectionKeyPermission GetMemoryProtectionKeyPermission();

  bool HasMemoryProtectionKeySupport();

 private:
  ~PKU() { this->FreeMemoryProtectionKey(); }

  PKU(const PKU&) = delete;

  PKU& operator=(const PKU&) = delete;

  void InitializeMemoryProtectionKeySupport();

  int AllocateMemoryProtectionKey();

  void FreeMemoryProtectionKey();

  using pkey_alloc_t = int (*)(unsigned, unsigned);
  using pkey_free_t = int (*)(int);
  using pkey_mprotect_t = int (*)(void*, size_t, int, int);
  using pkey_get_t = int (*)(int);
  using pkey_set_t = int (*)(int, unsigned);

  pkey_alloc_t pkey_alloc = nullptr;
  pkey_free_t pkey_free = nullptr;
  pkey_mprotect_t pkey_mprotect = nullptr;
  pkey_get_t pkey_get = nullptr;
  pkey_set_t pkey_set = nullptr;

  bool is_support_pku_ = false;

  int pku_key_;
};
#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_PLATFORM_PLATFORM_POSIX_H_
