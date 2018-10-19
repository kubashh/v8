// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>

#include "include/v8.h"
#include "src/trap-handler/trap-handler.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

#if V8_TRAP_HANDLER_SUPPORTED

void CrashOnPurpose() { *reinterpret_cast<volatile int*>(42); }
bool handler_got_executed = false;

// When using V8::RegisterDefaultSignalHandler, we save the old one to fall back
// on if V8 doesn't handle the signal. This allows tools like ASan to register a
// handler early on during the process startup and still generate stack traces
// on failures.
class SignalHandlerFallbackTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Register this handler as the last handler.
    constexpr ULONG last = 0;
    registered_handler_ = AddVectoredExceptionHandler(last, TestHandler);
    CHECK_NOT_NULL(registered_handler_);
  }

  void TearDown() override {
    // be a good citizen and restore the old signal handler.
    ULONG result = RemoveVectoredExceptionHandler(registered_handler_);
    CHECK(result);
  }

 private:
  static LONG TestHandler(LPEXCEPTION_POINTERS exception) {
    handler_got_executed = true;
    // Continue the execution one instruction after the faulting instruction.
    exception->ContextRecord->Rip++;
    return EXCEPTION_CONTINUE_EXECUTION;
  }

  void* registered_handler_;
};

TEST_F(SignalHandlerFallbackTest, DoTest) {
  constexpr bool use_default_signal_handler = true;
  CHECK(v8::V8::EnableWebAssemblyTrapHandler(use_default_signal_handler));
  CrashOnPurpose();
  CHECK(handler_got_executed);
  v8::internal::trap_handler::RestoreOriginalHandler();
}

#endif

}  //  namespace
