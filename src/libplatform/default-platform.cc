// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/default-platform.h"

#include <algorithm>
#include <queue>

#include "include/libplatform/libplatform.h"
#include "src/base/debug/stack_trace.h"
#include "src/base/lazy-instance.h"
#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/base/sys-info.h"
#include "src/base/utils/random-number-generator.h"
#include "src/libplatform/worker-thread.h"

namespace v8 {
namespace platform {

namespace {

void PrintStackTrace() {
  v8::base::debug::StackTrace trace;
  trace.Print();
  // Avoid dumping duplicate stack trace on abort signal.
  v8::base::debug::DisableSignalStackDump();
}

static base::LazyInstance<base::RandomNumberGenerator>::type
    random_number_generator = LAZY_INSTANCE_INITIALIZER;

}  // namespace

v8::Platform* CreateDefaultPlatform(
    int thread_pool_size, IdleTaskSupport idle_task_support,
    InProcessStackDumping in_process_stack_dumping,
    v8::TracingController* tracing_controller) {
  if (in_process_stack_dumping == InProcessStackDumping::kEnabled) {
    v8::base::debug::EnableInProcessStackDumping();
  }
  DefaultPlatform* platform =
      new DefaultPlatform(idle_task_support, tracing_controller);
  platform->SetThreadPoolSize(thread_pool_size);
  platform->EnsureInitialized();
  return platform;
}

bool PumpMessageLoop(v8::Platform* platform, v8::Isolate* isolate,
                     MessageLoopBehavior behavior) {
  return static_cast<DefaultPlatform*>(platform)->PumpMessageLoop(isolate,
                                                                  behavior);
}

void EnsureEventLoopInitialized(v8::Platform* platform, v8::Isolate* isolate) {
  return static_cast<DefaultPlatform*>(platform)->EnsureEventLoopInitialized(
      isolate);
}

void RunIdleTasks(v8::Platform* platform, v8::Isolate* isolate,
                  double idle_time_in_seconds) {
  static_cast<DefaultPlatform*>(platform)->RunIdleTasks(isolate,
                                                        idle_time_in_seconds);
}

void SetTracingController(
    v8::Platform* platform,
    v8::platform::tracing::TracingController* tracing_controller) {
  static_cast<DefaultPlatform*>(platform)->SetTracingController(
      tracing_controller);
}

const int DefaultPlatform::kMaxThreadPoolSize = 8;

DefaultPlatform::DefaultPlatform(IdleTaskSupport idle_task_support,
                                 v8::TracingController* tracing_controller)
    : initialized_(false),
      thread_pool_size_(0),
      idle_task_support_(idle_task_support) {
  if (tracing_controller) {
    tracing_controller_.reset(tracing_controller);
  } else {
    tracing::TracingController* controller = new tracing::TracingController();
    controller->Initialize(nullptr);
    tracing_controller_.reset(controller);
  }
}

DefaultPlatform::~DefaultPlatform() {
  base::LockGuard<base::Mutex> guard(&lock_);
  queue_.Terminate();
  if (initialized_) {
    for (auto i = thread_pool_.begin(); i != thread_pool_.end(); ++i) {
      delete *i;
    }
  }
  for (auto i = main_thread_queue_.begin(); i != main_thread_queue_.end();
       ++i) {
    while (!i->second.empty()) {
      delete i->second.front();
      i->second.pop();
    }
  }
  for (auto i = main_thread_delayed_queue_.begin();
       i != main_thread_delayed_queue_.end(); ++i) {
    while (!i->second.empty()) {
      delete i->second.top().second;
      i->second.pop();
    }
  }
  for (auto& i : main_thread_idle_queue_) {
    while (!i.second.empty()) {
      delete i.second.front();
      i.second.pop();
    }
  }
}

void* DefaultPlatform::GetRandomMmapAddr() {
  uintptr_t raw_addr;
  random_number_generator.Pointer()->NextBytes(&raw_addr, sizeof(raw_addr));
#if V8_OS_POSIX
#if V8_TARGET_ARCH_X64
  // Currently available CPUs have 48 bits of virtual addressing.  Truncate
  // the hint address to 46 bits to give the kernel a fighting chance of
  // fulfilling our placement request.
  raw_addr &= V8_UINT64_C(0x3ffffffff000);
#elif V8_TARGET_ARCH_PPC64
#if V8_OS_AIX
  // AIX: 64 bits of virtual addressing, but we limit address range to:
  //   a) minimize Segment Lookaside Buffer (SLB) misses and
  raw_addr &= V8_UINT64_C(0x3ffff000);
  // Use extra address space to isolate the mmap regions.
  raw_addr += V8_UINT64_C(0x400000000000);
#elif V8_TARGET_BIG_ENDIAN
  // Big-endian Linux: 44 bits of virtual addressing.
  raw_addr &= V8_UINT64_C(0x03fffffff000);
#else
  // Little-endian Linux: 48 bits of virtual addressing.
  raw_addr &= V8_UINT64_C(0x3ffffffff000);
#endif
#elif V8_TARGET_ARCH_S390X
  // Linux on Z uses bits 22-32 for Region Indexing, which translates to 42 bits
  // of virtual addressing.  Truncate to 40 bits to allow kernel chance to
  // fulfill request.
  raw_addr &= V8_UINT64_C(0xfffffff000);
#elif V8_TARGET_ARCH_S390
  // 31 bits of virtual addressing.  Truncate to 29 bits to allow kernel chance
  // to fulfill request.
  raw_addr &= 0x1ffff000;
#else
  raw_addr &= 0x3ffff000;

#ifdef __sun
  // For our Solaris/illumos mmap hint, we pick a random address in the bottom
  // half of the top half of the address space (that is, the third quarter).
  // Because we do not MAP_FIXED, this will be treated only as a hint -- the
  // system will not fail to mmap() because something else happens to already
  // be mapped at our random address. We deliberately set the hint high enough
  // to get well above the system's break (that is, the heap); Solaris and
  // illumos will try the hint and if that fails allocate as if there were
  // no hint at all. The high hint prevents the break from getting hemmed in
  // at low values, ceding half of the address space to the system heap.
  raw_addr += 0x80000000;
#elif V8_OS_AIX
  // The range 0x30000000 - 0xD0000000 is available on AIX;
  // choose the upper range.
  raw_addr += 0x90000000;
#else
  // The range 0x20000000 - 0x60000000 is relatively unpopulated across a
  // variety of ASLR modes (PAE kernel, NX compat mode, etc) and on macos
  // 10.6 and 10.7.
  raw_addr += 0x20000000;
#endif
#endif
#else  // V8_OS_WIN
// The address range used to randomize RWX allocations in OS::Allocate
// Try not to map pages into the default range that windows loads DLLs
// Use a multiple of 64k to prevent committing unused memory.
// Note: This does not guarantee RWX regions will be within the
// range kAllocationRandomAddressMin to kAllocationRandomAddressMax
#ifdef V8_HOST_ARCH_64_BIT
  static const uintptr_t kAllocationRandomAddressMin = 0x0000000080000000;
  static const uintptr_t kAllocationRandomAddressMax = 0x000003FFFFFF0000;
#else
  static const uintptr_t kAllocationRandomAddressMin = 0x04000000;
  static const uintptr_t kAllocationRandomAddressMax = 0x3FFF0000;
#endif
  raw_addr <<= kPageSizeBits;
  raw_addr += kAllocationRandomAddressMin;
  raw_addr &= kAllocationRandomAddressMax;
#endif  // V8_OS_WIN
  return reinterpret_cast<void*>(raw_addr);
}

void DefaultPlatform::SetThreadPoolSize(int thread_pool_size) {
  base::LockGuard<base::Mutex> guard(&lock_);
  DCHECK(thread_pool_size >= 0);
  if (thread_pool_size < 1) {
    thread_pool_size = base::SysInfo::NumberOfProcessors() - 1;
  }
  thread_pool_size_ =
      std::max(std::min(thread_pool_size, kMaxThreadPoolSize), 1);
}


void DefaultPlatform::EnsureInitialized() {
  base::LockGuard<base::Mutex> guard(&lock_);
  if (initialized_) return;
  initialized_ = true;

  for (int i = 0; i < thread_pool_size_; ++i)
    thread_pool_.push_back(new WorkerThread(&queue_));
}


Task* DefaultPlatform::PopTaskInMainThreadQueue(v8::Isolate* isolate) {
  auto it = main_thread_queue_.find(isolate);
  if (it == main_thread_queue_.end() || it->second.empty()) {
    return NULL;
  }
  Task* task = it->second.front();
  it->second.pop();
  return task;
}


Task* DefaultPlatform::PopTaskInMainThreadDelayedQueue(v8::Isolate* isolate) {
  auto it = main_thread_delayed_queue_.find(isolate);
  if (it == main_thread_delayed_queue_.end() || it->second.empty()) {
    return NULL;
  }
  double now = MonotonicallyIncreasingTime();
  std::pair<double, Task*> deadline_and_task = it->second.top();
  if (deadline_and_task.first > now) {
    return NULL;
  }
  it->second.pop();
  return deadline_and_task.second;
}

IdleTask* DefaultPlatform::PopTaskInMainThreadIdleQueue(v8::Isolate* isolate) {
  auto it = main_thread_idle_queue_.find(isolate);
  if (it == main_thread_idle_queue_.end() || it->second.empty()) {
    return nullptr;
  }
  IdleTask* task = it->second.front();
  it->second.pop();
  return task;
}

void DefaultPlatform::EnsureEventLoopInitialized(v8::Isolate* isolate) {
  base::LockGuard<base::Mutex> guard(&lock_);
  if (event_loop_control_.count(isolate) == 0) {
    event_loop_control_.insert(std::make_pair(
        isolate, std::unique_ptr<base::Semaphore>(new base::Semaphore(0))));
  }
}

void DefaultPlatform::WaitForForegroundWork(v8::Isolate* isolate) {
  base::Semaphore* semaphore = nullptr;
  {
    base::LockGuard<base::Mutex> guard(&lock_);
    DCHECK_EQ(event_loop_control_.count(isolate), 1);
    semaphore = event_loop_control_[isolate].get();
  }
  DCHECK_NOT_NULL(semaphore);
  semaphore->Wait();
}

bool DefaultPlatform::PumpMessageLoop(v8::Isolate* isolate,
                                      MessageLoopBehavior behavior) {
  if (behavior == MessageLoopBehavior::kWaitForWork) {
    WaitForForegroundWork(isolate);
  }
  Task* task = NULL;
  {
    base::LockGuard<base::Mutex> guard(&lock_);

    // Move delayed tasks that hit their deadline to the main queue.
    task = PopTaskInMainThreadDelayedQueue(isolate);
    while (task != NULL) {
      ScheduleOnForegroundThread(isolate, task);
      task = PopTaskInMainThreadDelayedQueue(isolate);
    }

    task = PopTaskInMainThreadQueue(isolate);

    if (task == NULL) {
      return behavior == MessageLoopBehavior::kWaitForWork;
    }
  }
  task->Run();
  delete task;
  return true;
}

void DefaultPlatform::RunIdleTasks(v8::Isolate* isolate,
                                   double idle_time_in_seconds) {
  DCHECK(IdleTaskSupport::kEnabled == idle_task_support_);
  double deadline_in_seconds =
      MonotonicallyIncreasingTime() + idle_time_in_seconds;
  while (deadline_in_seconds > MonotonicallyIncreasingTime()) {
    {
      IdleTask* task;
      {
        base::LockGuard<base::Mutex> guard(&lock_);
        task = PopTaskInMainThreadIdleQueue(isolate);
      }
      if (task == nullptr) return;
      task->Run(deadline_in_seconds);
      delete task;
    }
  }
}

void DefaultPlatform::CallOnBackgroundThread(Task* task,
                                             ExpectedRuntime expected_runtime) {
  EnsureInitialized();
  queue_.Append(task);
}

void DefaultPlatform::ScheduleOnForegroundThread(v8::Isolate* isolate,
                                                 Task* task) {
  main_thread_queue_[isolate].push(task);
  if (event_loop_control_.count(isolate) != 0) {
    event_loop_control_[isolate]->Signal();
  }
}

void DefaultPlatform::CallOnForegroundThread(v8::Isolate* isolate, Task* task) {
  base::LockGuard<base::Mutex> guard(&lock_);
  ScheduleOnForegroundThread(isolate, task);
}


void DefaultPlatform::CallDelayedOnForegroundThread(Isolate* isolate,
                                                    Task* task,
                                                    double delay_in_seconds) {
  base::LockGuard<base::Mutex> guard(&lock_);
  double deadline = MonotonicallyIncreasingTime() + delay_in_seconds;
  main_thread_delayed_queue_[isolate].push(std::make_pair(deadline, task));
}

void DefaultPlatform::CallIdleOnForegroundThread(Isolate* isolate,
                                                 IdleTask* task) {
  base::LockGuard<base::Mutex> guard(&lock_);
  main_thread_idle_queue_[isolate].push(task);
}

bool DefaultPlatform::IdleTasksEnabled(Isolate* isolate) {
  return idle_task_support_ == IdleTaskSupport::kEnabled;
}

double DefaultPlatform::MonotonicallyIncreasingTime() {
  return base::TimeTicks::HighResolutionNow().ToInternalValue() /
         static_cast<double>(base::Time::kMicrosecondsPerSecond);
}

double DefaultPlatform::CurrentClockTimeMillis() {
  return base::OS::TimeCurrentMillis();
}

TracingController* DefaultPlatform::GetTracingController() {
  return tracing_controller_.get();
}

void DefaultPlatform::SetTracingController(
    v8::TracingController* tracing_controller) {
  DCHECK_NOT_NULL(tracing_controller);
  tracing_controller_.reset(tracing_controller);
}

size_t DefaultPlatform::NumberOfAvailableBackgroundThreads() {
  return static_cast<size_t>(thread_pool_size_);
}

Platform::StackTracePrinter DefaultPlatform::GetStackTracePrinter() {
  return PrintStackTrace;
}

}  // namespace platform
}  // namespace v8
