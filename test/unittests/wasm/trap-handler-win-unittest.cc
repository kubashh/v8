// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>

#include "include/v8.h"
#include "src/allocation.h"
#include "src/base/page-allocator.h"
#include "src/trap-handler/trap-handler.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

#if V8_TRAP_HANDLER_SUPPORTED

void CrashOnPurpose() { *reinterpret_cast<volatile int*>(42); }
bool g_handler_got_executed = false;

// When using V8::RegisterDefaultSignalHandler, we save the old one to fall back
// on if V8 doesn't handle the signal. This allows tools like ASan to register a
// handler early on during the process startup and still generate stack traces
// on failures.
class SignalHandlerFallbackTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Register this handler as the last handler.
    constexpr ULONG last = 0;
    registered_handler_ = AddVectoredExceptionHandler(/*first=*/0, TestHandler);
    CHECK_NOT_NULL(registered_handler_);

    v8::PageAllocator* page_allocator = GetPlatformPageAllocator();
    // We only need a single page.
    size_t size = page_allocator->AllocatePageSize();
    void* hint = page_allocator->GetRandomMmapAddr();
    VirtualMemory mem(page_allocator, size, hint, size);
    // Set the permissions of the memory to no-access.
    mem.SetPermissions(mem.address(), size, PageAllocator::kNoAccess);
    mem_ = std::move(mem);
  }

  void AccessTestMemory() { *reinterpret_cast<int*>(mem_.address()) = 42; }

  void TearDown() override {
    // be a good citizen and restore the old signal handler.
    ULONG result = RemoveVectoredExceptionHandler(registered_handler_);
    CHECK(result);
  }

 private:
  static LONG WINAPI TestHandler(EXCEPTION_POINTERS* exception) {
    g_handler_got_executed = true;
    i::SetPermissions(GetPlatformPageAllocator(),
                      exception->ExceptionRecord->ExceptionInformation[1],
                      PageAllocator::kReadWrite);
    return EXCEPTION_CONTINUE_EXECUTION;
  }

  VirtualMemory mem_;
  void* registered_handler_;
};

TEST_F(SignalHandlerFallbackTest, DoTest) {
  constexpr bool use_default_signal_handler = true;
  CHECK(v8::V8::EnableWebAssemblyTrapHandler(use_default_signal_handler));
  AccessTestMemory();
  CHECK(g_handler_got_executed);
  v8::internal::trap_handler::RemoveTrapHandler();
}

#endif

}  //  namespace
