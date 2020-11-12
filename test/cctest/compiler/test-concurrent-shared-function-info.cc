// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "src/api/api-inl.h"
#include "src/codegen/compiler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/pipeline.h"
#include "src/handles/handles.h"
#include "src/logging/counters.h"
#include "src/objects/js-function.h"
#include "src/objects/shared-function-info.h"
#include "src/utils/utils-inl.h"
#include "src/zone/zone.h"
#include "test/cctest/cctest.h"
#include "test/common/flag-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

enum class SfiState {
  Uncompiled,
  Compiled,
  DebugInfo,
  PreparedForDebugExecution,
};

void CheckSharedFunctionInfoState(SharedFunctionInfo sfi,
                                  SfiState expectedState) {
  Object data = sfi.function_data(kAcquireLoad);
  HeapObject script = sfi.script_or_debug_info(kAcquireLoad);
  switch (expectedState) {
    case SfiState::Uncompiled:
      CHECK(data.IsUncompiledData());
      CHECK(script.IsScript());
      break;
    case SfiState::Compiled:
      CHECK(data.IsBytecodeArray());
      CHECK(script.IsScript());
      break;
    case SfiState::DebugInfo:
      CHECK(data.IsBytecodeArray());
      CHECK(script.IsDebugInfo());
      {
        DebugInfo di = DebugInfo::cast(script);
        CHECK(!di.HasInstrumentedBytecodeArray());
      }
      break;
    case SfiState::PreparedForDebugExecution:
      CHECK(data.IsBytecodeArray());
      CHECK(script.IsDebugInfo());
      {
        DebugInfo di = DebugInfo::cast(script);
        CHECK(di.HasInstrumentedBytecodeArray());
      }
      break;
  }
}

class BackgroundCompilationThread final : public v8::base::Thread {
 public:
  BackgroundCompilationThread(Isolate* isolate,
                              base::Semaphore* sema_execute_start,
                              base::Semaphore* sema_execute_complete,
                              OptimizedCompilationJob* job)
      : base::Thread(base::Thread::Options("BackgroundCompilationThread")),
        isolate_(isolate),
        sema_execute_start_(sema_execute_start),
        sema_execute_complete_(sema_execute_complete),
        job_(job) {}

  void Run() override {
    RuntimeCallStats stats(RuntimeCallStats::kWorkerThread);
    LocalIsolate local_isolate(isolate_, ThreadKind::kBackground);
    sema_execute_start_->Wait();
    const CompilationJob::Status status =
        job_->ExecuteJob(&stats, &local_isolate);
    CHECK_EQ(status, CompilationJob::SUCCEEDED);
    sema_execute_complete_->Signal();
  }

 private:
  Isolate* isolate_;
  base::Semaphore* sema_execute_start_;
  base::Semaphore* sema_execute_complete_;
  OptimizedCompilationJob* job_;
};

TEST(TestConcurrentSharedFunctionInfo) {
  FlagScope<bool> allow_natives_syntax(&i::FLAG_allow_natives_syntax, true);
  FlagScope<bool> concurrent_inlining(&i::FLAG_concurrent_inlining, true);
  FlagScope<bool> turbo_direct_heap_reads(&i::FLAG_turbo_direct_heap_access,
                                          true);
  FlagScope<bool> trace_opt(&i::FLAG_trace_opt, true);
  FlagScope<bool> trace_deopt(&i::FLAG_trace_deopt, true);
  FlagScope<bool> trace_inlining(&i::FLAG_trace_turbo_inlining, true);

  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();

  Zone zone(isolate->allocator(), ZONE_NAME);
  HandleScope handle_scope(isolate);

  const char* source_code =
      "function f(x, y) { return x + y; }\n"
      "function test(x) { return f(f(1, x), f(x, 1)); }\n"
      "%PrepareFunctionForOptimization(f);\n"
      "%PrepareFunctionForOptimization(test);\n"
      "test(3);\n"
      "test(-9);\n";

  CompileRun(source_code);

  // function test
  Local<Function> function_test = Local<Function>::Cast(
      CcTest::global()
          ->Get(CcTest::isolate()->GetCurrentContext(), v8_str("test"))
          .ToLocalChecked());
  Handle<JSFunction> test =
      Handle<JSFunction>::cast(v8::Utils::OpenHandle(*function_test));
  Handle<SharedFunctionInfo> test_sfi(test->shared(), isolate);
  DCHECK(test_sfi->HasBytecodeArray());
  IsCompiledScope compiled_scope(*test_sfi, isolate);
  JSFunction::EnsureFeedbackVector(test, &compiled_scope);

  // function f
  Local<Function> function_f = Local<Function>::Cast(
      CcTest::global()
          ->Get(CcTest::isolate()->GetCurrentContext(), v8_str("f"))
          .ToLocalChecked());
  Handle<JSFunction> f =
      Handle<JSFunction>::cast(v8::Utils::OpenHandle(*function_f));
  Handle<SharedFunctionInfo> f_sfi(f->shared(), isolate);
  DCHECK(f_sfi->HasBytecodeArray());
  OptimizedCompilationInfo f_info(&zone, isolate, f_sfi, f, CodeKind::TURBOFAN);
  Handle<Code> f_code =
      Pipeline::GenerateCodeForTesting(&f_info, isolate).ToHandleChecked();
  f->set_code(*f_code);
  {
    IsCompiledScope compiled_scope(*f_sfi, isolate);
    JSFunction::EnsureFeedbackVector(f, &compiled_scope);
  }

  CheckSharedFunctionInfoState(*test_sfi, SfiState::Compiled);

  auto job =
      Pipeline::NewCompilationJob(isolate, test, CodeKind::TURBOFAN, true);

  // Prepare job.
  {
    CompilationHandleScope compilation(isolate, job->compilation_info());
    CanonicalHandleScope canonical(isolate, job->compilation_info());
    job->compilation_info()->ReopenHandlesInNewHandleScope(isolate);
    const CompilationJob::Status status = job->PrepareJob(isolate);
    CHECK_EQ(status, CompilationJob::SUCCEEDED);
  }

  // Start a background thread to execute the compilation job.
  base::Semaphore sema_execute_start(0);
  base::Semaphore sema_execute_complete(0);
  BackgroundCompilationThread thread(isolate, &sema_execute_start,
                                     &sema_execute_complete, job.get());
  CHECK(thread.Start());

  sema_execute_start.Signal();
  // Background thread is running, now mess with test's SFI.
  CheckSharedFunctionInfoState(*test_sfi, SfiState::Compiled);

  // Compiled ==> DebugInfo
  {
    isolate->debug()->GetOrCreateDebugInfo(test_sfi);
    CheckSharedFunctionInfoState(*test_sfi, SfiState::DebugInfo);
  }

  for (int i = 0; i < 100; ++i) {
    // DebugInfo ==> PreparedForDebugExecution
    {
      int breakpoint_id;
      isolate->debug()->SetBreakpointForFunction(
          test_sfi, isolate->factory()->empty_string(), &breakpoint_id);
      CheckSharedFunctionInfoState(*test_sfi,
                                   SfiState::PreparedForDebugExecution);
    }

    // PreparedForDebugExecution ==> DebugInfo
    {
      DebugInfo debug_info = test_sfi->GetDebugInfo();
      debug_info.ClearBreakInfo(isolate);
      CheckSharedFunctionInfoState(*test_sfi, SfiState::DebugInfo);
    }
  }

  sema_execute_complete.Wait();

  // Finalize job.
  {
    const CompilationJob::Status status = job->FinalizeJob(isolate);
    CHECK_EQ(status, CompilationJob::SUCCEEDED);
    CHECK(job->compilation_info()->has_bytecode_array());
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
