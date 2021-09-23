// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline-batch-compiler.h"

// TODO(v8:11421): Remove #if once baseline compiler is ported to other
// architectures.
#include "src/flags/flags.h"
#if ENABLE_SPARKPLUG

#include "src/baseline/baseline-compiler.h"
#include "src/codegen/compiler.h"
#include "src/execution/isolate.h"
#include "src/handles/global-handles-inl.h"
#include "src/heap/factory-inl.h"
#include "src/heap/heap-inl.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/parked-scope.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/js-function-inl.h"

namespace v8 {
namespace internal {
namespace baseline {

class BaselineCompilerTask {
 public:
  BaselineCompilerTask(Isolate* isolate, SharedFunctionInfo shared)
      : isolate_(isolate) {
    DCHECK(shared.is_compiled());
    handles_ = isolate_->NewPersistentHandles();
    shared_ = handles_->NewHandle(shared);
    bytecode_ = handles_->NewHandle(shared.GetBytecodeArray(isolate));
  }

  BaselineCompilerTask(const BaselineCompilerTask&) V8_NOEXCEPT = delete;
  BaselineCompilerTask(BaselineCompilerTask&&) V8_NOEXCEPT = default;

  void Run() {
#ifdef V8_RUNTIME_CALL_STATS
    WorkerThreadRuntimeCallStatsScope runtime_call_stats_scope(
        isolate_->counters()->worker_thread_runtime_call_stats());
    LocalIsolate local_isolate(isolate_, ThreadKind::kBackground,
                               runtime_call_stats_scope.Get());
#else
    LocalIsolate local_isolate(isolate_, ThreadKind::kBackground);
#endif

    local_isolate.heap()->AttachPersistentHandles(std::move(handles_));
    UnparkedScope unparked_scope(&local_isolate);
    LocalHandleScope handle_scope(&local_isolate);
    {
      BaselineCompiler compiler(isolate_, shared_, bytecode_);
      compiler.SetLocalIsolate(&local_isolate);
      compiler.GenerateCode();

      MaybeHandle<Code> maybe_code = compiler.Build(isolate_, &local_isolate);
      if (!maybe_code.is_null()) {
        Handle<Code> code = maybe_code.ToHandleChecked();
        if (FLAG_print_code) {
          code->Print();
        }
        local_isolate.heap()->RegisterCodeObject(code);
        shared_->set_baseline_code(*code, kReleaseStore);
        if (V8_LIKELY(FLAG_use_osr)) {
          // Arm back edges for OSR
          shared_->GetBytecodeArray(&local_isolate)
              .set_osr_loop_nesting_level(AbstractCode::kMaxLoopNestingMarker);
        }
        if (FLAG_trace_baseline_concurrent_compilation) {
          CodeTracer::Scope scope(isolate_->GetCodeTracer());
          // Using a string buffer, otherwise we would print the info into
          // multiple lines due to thread interleaving.
          std::stringstream ss;
          ss << "Concurrent Sparkplug Off thread] Function ";
          shared_->ShortPrint(ss);
          ss << " compiled\n";
          OFStream os(scope.file());
          os << ss.str();
        }
      }
    }
  }

 private:
  Isolate* isolate_;
  Handle<SharedFunctionInfo> shared_;
  Handle<BytecodeArray> bytecode_;
  std::unique_ptr<PersistentHandles> handles_;
};

class BaselineBatchCompilerJob : public v8::JobTask {
 public:
  BaselineBatchCompilerJob(Isolate* isolate, Handle<WeakFixedArray> task_queue,
                           int batch_size)
      : isolate_(isolate) {
    tasks_.reserve(batch_size);
    int num_tasks = batch_size;
    for (int i = batch_size - 1; i >= 0; i--) {
      MaybeObject maybe_sfi = task_queue->Get(i);
      // TODO(victorgomes): Do I need to clear the value?
      task_queue->Set(i, HeapObjectReference::ClearedValue(isolate_));
      HeapObject obj;
      // Skip functions where weak reference is no longer valid.
      if (!maybe_sfi.GetHeapObjectIfWeak(&obj)) {
        num_tasks--;
        continue;
      }
      SharedFunctionInfo shared = SharedFunctionInfo::cast(obj);
      // Skip functions where the bytecode has been flushed.
      if (ShouldSkipFunction(shared)) {
        num_tasks--;
        continue;
      }
      tasks_.emplace_back(isolate_, shared);
    }
    if (FLAG_trace_baseline_concurrent_compilation) {
      CodeTracer::Scope scope(isolate_->GetCodeTracer());
      PrintF(scope.file(), "[Concurrent Sparkplug] compiling %d functions\n",
             num_tasks);
    }
    remaining_tasks_ = num_tasks;
  }

  bool ShouldSkipFunction(SharedFunctionInfo shared) {
    return !shared.is_compiled() || shared.HasBaselineCode() ||
           !CanCompileWithBaseline(isolate_, shared);
  }

  void Run(JobDelegate* delegate) override {
    int task = --remaining_tasks_;
    DCHECK_LT(task, tasks_.size());
    tasks_[task].Run();
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    return remaining_tasks_.load(std::memory_order_relaxed);
  }

 private:
  Isolate* isolate_;
  std::atomic<int> remaining_tasks_{0};
  std::vector<BaselineCompilerTask> tasks_;
};

BaselineBatchCompiler::BaselineBatchCompiler(Isolate* isolate)
    : isolate_(isolate),
      compilation_queue_(Handle<WeakFixedArray>::null()),
      last_index_(0),
      estimated_instruction_size_(0),
      enabled_(true) {}

BaselineBatchCompiler::~BaselineBatchCompiler() {
  if (!compilation_queue_.is_null()) {
    GlobalHandles::Destroy(compilation_queue_.location());
    compilation_queue_ = Handle<WeakFixedArray>::null();
  }
  if (current_job_ && current_job_->IsValid()) {
    // Cancel the compilation job.
    current_job_->Cancel();
  }
}

void BaselineBatchCompiler::EnqueueFunction(Handle<JSFunction> function) {
  Handle<SharedFunctionInfo> shared(function->shared(), isolate_);
  // Early return if the function is compiled with baseline already or it is not
  // suitable for baseline compilation.
  if (shared->HasBaselineCode()) return;
  if (!CanCompileWithBaseline(isolate_, *shared)) return;

  // Immediately compile the function if batch compilation is disabled.
  if (!is_enabled()) {
    IsCompiledScope is_compiled_scope(
        function->shared().is_compiled_scope(isolate_));
    Compiler::CompileBaseline(isolate_, function, Compiler::CLEAR_EXCEPTION,
                              &is_compiled_scope);
    return;
  }

  int estimated_size;
  {
    DisallowHeapAllocation no_gc;
    estimated_size = BaselineCompiler::EstimateInstructionSize(
        shared->GetBytecodeArray(isolate_));
  }
  estimated_instruction_size_ += estimated_size;
  if (FLAG_trace_baseline_batch_compilation) {
    CodeTracer::Scope trace_scope(isolate_->GetCodeTracer());
    PrintF(trace_scope.file(),
           "[Baseline batch compilation] Enqueued function ");
    function->PrintName(trace_scope.file());
    PrintF(trace_scope.file(),
           " with estimated size %d (current budget: %d/%d)\n", estimated_size,
           estimated_instruction_size_,
           FLAG_baseline_batch_compilation_threshold);
  }
  if (ShouldCompileBatch()) {
    if (FLAG_trace_baseline_batch_compilation) {
      CodeTracer::Scope trace_scope(isolate_->GetCodeTracer());
      PrintF(trace_scope.file(),
             "[Baseline batch compilation] Compiling current batch of %d "
             "functions\n",
             (last_index_ + 1));
    }
    if (FLAG_concurrent_sparkplug) {
      Enqueue(shared);
      CompileBatchConcurrently();
      ClearBatch();
    } else {
      CompileBatch(function);
    }
  } else {
    Enqueue(shared);
  }
}

void BaselineBatchCompiler::CompileBatchConcurrently() {
  DCHECK(FLAG_concurrent_sparkplug);
  RCS_SCOPE(isolate_, RuntimeCallCounterId::kCompileBaseline);
  current_job_ = V8::GetCurrentPlatform()->PostJob(
      TaskPriority::kUserVisible,
      std::make_unique<BaselineBatchCompilerJob>(isolate_, compilation_queue_,
                                                 last_index_ + 1));
}

void BaselineBatchCompiler::Enqueue(Handle<SharedFunctionInfo> shared) {
  EnsureQueueCapacity();
  compilation_queue_->Set(last_index_++, HeapObjectReference::Weak(*shared));
}

void BaselineBatchCompiler::EnsureQueueCapacity() {
  if (compilation_queue_.is_null()) {
    compilation_queue_ = isolate_->global_handles()->Create(
        *isolate_->factory()->NewWeakFixedArray(kInitialQueueSize,
                                                AllocationType::kOld));
    return;
  }
  if (last_index_ >= compilation_queue_->length()) {
    Handle<WeakFixedArray> new_queue =
        isolate_->factory()->CopyWeakFixedArrayAndGrow(compilation_queue_,
                                                       last_index_);
    GlobalHandles::Destroy(compilation_queue_.location());
    compilation_queue_ = isolate_->global_handles()->Create(*new_queue);
  }
}

void BaselineBatchCompiler::CompileBatch(Handle<JSFunction> function) {
  CodePageCollectionMemoryModificationScope batch_allocation(isolate_->heap());
  {
    IsCompiledScope is_compiled_scope(
        function->shared().is_compiled_scope(isolate_));
    Compiler::CompileBaseline(isolate_, function, Compiler::CLEAR_EXCEPTION,
                              &is_compiled_scope);
  }
  for (int i = 0; i < last_index_; i++) {
    MaybeObject maybe_sfi = compilation_queue_->Get(i);
    MaybeCompileFunction(maybe_sfi);
    compilation_queue_->Set(i, HeapObjectReference::ClearedValue(isolate_));
  }
  ClearBatch();
}

bool BaselineBatchCompiler::ShouldCompileBatch() const {
  if (current_job_) {
    if (current_job_->IsValid() && current_job_->IsActive()) return false;
    current_job_->Cancel();  // Guarantees empty thread state.
  }
  return estimated_instruction_size_ >=
         FLAG_baseline_batch_compilation_threshold;
}

bool BaselineBatchCompiler::MaybeCompileFunction(MaybeObject maybe_sfi) {
  HeapObject heapobj;
  // Skip functions where the weak reference is no longer valid.
  if (!maybe_sfi.GetHeapObjectIfWeak(&heapobj)) return false;
  Handle<SharedFunctionInfo> shared =
      handle(SharedFunctionInfo::cast(heapobj), isolate_);
  // Skip functions where the bytecode has been flushed.
  if (!shared->is_compiled()) return false;

  IsCompiledScope is_compiled_scope(shared->is_compiled_scope(isolate_));
  return Compiler::CompileSharedWithBaseline(
      isolate_, shared, Compiler::CLEAR_EXCEPTION, &is_compiled_scope);
}

void BaselineBatchCompiler::ClearBatch() {
  estimated_instruction_size_ = 0;
  last_index_ = 0;
}

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#else

namespace v8 {
namespace internal {
namespace baseline {

BaselineBatchCompiler::BaselineBatchCompiler(Isolate* isolate)
    : isolate_(isolate),
      compilation_queue_(Handle<WeakFixedArray>::null()),
      last_index_(0),
      estimated_instruction_size_(0),
      enabled_(false) {}

BaselineBatchCompiler::~BaselineBatchCompiler() {
  if (!compilation_queue_.is_null()) {
    GlobalHandles::Destroy(compilation_queue_.location());
    compilation_queue_ = Handle<WeakFixedArray>::null();
  }
}

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif
