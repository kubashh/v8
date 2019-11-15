// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

#include "src/wasm/gdb-server/gdb-server.h"

#include <inttypes.h>
#include "src/api/api-inl.h"
#include "src/api/api.h"
#include "src/execution/frames.h"
#include "src/utils/locked-queue-inl.h"
#include "src/wasm/gdb-server/gdb-server-thread.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-objects.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

enum { kModuleEmbedderDataIndex, kInspectorClientIndex };

class TaskRunner {
 public:
  class Task {
   public:
    virtual ~Task() = default;
    virtual bool is_priority_task() = 0;
    virtual void Run() = 0;
  };

  TaskRunner();

  // Should be called from the same thread and only from task.
  void RunMessageLoop(bool only_protocol);
  void QuitMessageLoop();

  // TaskRunner takes ownership.
  void Append(Task* task);

  void Terminate();

 private:
  Task* GetNext(bool only_protocol);

  // deferred_queue_ combined with queue_ (in this order) have all tasks in the
  // correct order. Sometimes we skip non-protocol tasks by moving them from
  // queue_ to deferred_queue_.
  LockedQueue<Task*> queue_;
  LockedQueue<Task*> deferred_queue_;
  v8::base::Semaphore process_queue_semaphore_;

  int nested_loop_count_;

  std::atomic<int> is_terminated_;

  DISALLOW_COPY_AND_ASSIGN(TaskRunner);
};

TaskRunner::TaskRunner()
    : process_queue_semaphore_(0), nested_loop_count_(0), is_terminated_(0) {}

void TaskRunner::RunMessageLoop(bool only_protocol) {
  is_terminated_ = 0;
  int loop_number = ++nested_loop_count_;
  while (nested_loop_count_ == loop_number && !is_terminated_) {
    TaskRunner::Task* task = GetNext(only_protocol);
    if (!task) return;
    task->Run();
    delete task;
  }
}

void TaskRunner::QuitMessageLoop() {
  DCHECK_LT(0, nested_loop_count_);
  --nested_loop_count_;
}

void TaskRunner::Append(Task* task) {
  queue_.Enqueue(task);
  process_queue_semaphore_.Signal();
}

void TaskRunner::Terminate() {
  is_terminated_++;
  process_queue_semaphore_.Signal();
}

TaskRunner::Task* TaskRunner::GetNext(bool only_protocol) {
  for (;;) {
    if (is_terminated_) return nullptr;
    if (only_protocol) {
      Task* task = nullptr;
      if (queue_.Dequeue(&task)) {
        if (task->is_priority_task()) return task;
        deferred_queue_.Enqueue(task);
      }
    } else {
      Task* task = nullptr;
      if (deferred_queue_.Dequeue(&task)) return task;
      if (queue_.Dequeue(&task)) return task;
    }
    process_queue_semaphore_.Wait();
  }
  return nullptr;
}

template <typename T>
void RunSyncTask(TaskRunner* task_runner, T callback) {
  class SyncTask : public TaskRunner::Task {
   public:
    SyncTask(v8::base::Semaphore* ready_semaphore, T callback)
        : ready_semaphore_(ready_semaphore), callback_(callback) {}
    ~SyncTask() override = default;
    bool is_priority_task() final { return true; }

   private:
    void Run() override {
      callback_();
      if (ready_semaphore_) ready_semaphore_->Signal();
    }

    v8::base::Semaphore* ready_semaphore_;
    T callback_;
  };

  v8::base::Semaphore ready_semaphore(0);
  task_runner->Append(new SyncTask(&ready_semaphore, callback));
  ready_semaphore.Wait();
}

static const uint32_t kMaxWasmCallStack = 20;

GdbServer::WasmDebugScript::WasmDebugScript(
    v8::Isolate* isolate, Local<debug::WasmScript> wasm_script) {
  isolate_ = isolate;
  wasm_script_ = Global<debug::WasmScript>(isolate, wasm_script);
}

GdbServer::GdbServer(Isolate* isolate, WasmEngine* wasm_engine)
    : wasm_engine_(wasm_engine) {
  debug::ChangeBreakOnException(reinterpret_cast<v8::Isolate*>(isolate),
                                debug::BreakOnUncaughtException);
  AddIsolate(isolate);

  task_runner_ = std::make_unique<TaskRunner>();
  thread_ = std::make_unique<GdbServerThread>(this);
  if (!thread_->Start()) {
    TRACE_GDB_REMOTE(
        "Cannot initialize thread, GDB-remote debugging will be disabled.\n");
    thread_ = nullptr;
    return;
  }
}

GdbServer::~GdbServer() {
  if (thread_) {
    thread_->Stop();
    thread_->Join();
    thread_ = nullptr;
  }
}

void GdbServer::AddIsolate(Isolate* isolate) {
  base::MutexGuard guard(&mutex_);
  if (isolate_delegates_.find(isolate) == isolate_delegates_.end()) {
    isolate_delegates_[isolate] =
        std::make_unique<DebugDelegate>(isolate, this);
  }
}

void GdbServer::RemoveIsolate(Isolate* isolate) {
  base::MutexGuard guard(&mutex_);
  auto it = isolate_delegates_.find(isolate);
  if (it != isolate_delegates_.end()) {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
    for (auto it = scripts_.begin(); it != scripts_.end();) {
      if (it->second->isolate_ == v8_isolate) {
        it = scripts_.erase(it);
      } else {
        ++it;
      }
    }
    isolate_delegates_.erase(it);
  }
}

GdbServer::DebugDelegate::DebugDelegate(Isolate* isolate, GdbServer* gdb_server)
    : isolate_(isolate), gdb_server_(gdb_server) {
  isolate_->SetCaptureStackTraceForUncaughtExceptions(
      true, kMaxWasmCallStack, v8::StackTrace::kOverview);
  isolate_->debug()->SetDebugDelegate(this);
}

void GdbServer::DebugDelegate::ScriptCompiled(Local<debug::Script> script,
                                              bool is_live_edited,
                                              bool has_compile_error) {
  base::MutexGuard guard(&gdb_server_->mutex_);
  if (script->IsWasm()) {
    v8::Isolate* isolate = script->GetIsolate();
    DCHECK_EQ(reinterpret_cast<v8::Isolate*>(isolate_), isolate);
    gdb_server_->scripts_[script->Id()] = std::make_unique<WasmDebugScript>(
        WasmDebugScript(isolate, script.As<debug::WasmScript>()));
  }
}

std::vector<uint64_t> GdbServer::DebugDelegate::CalculateCallStack(
    v8::Local<v8::StackTrace> stack_trace) const {
  std::vector<uint64_t> call_stack;
  for (int i = 0; i < stack_trace->GetFrameCount(); i++) {
    v8::Local<v8::StackFrame> stack_frame =
        stack_trace->GetFrame(reinterpret_cast<v8::Isolate*>(isolate_), i);
    if (stack_frame
            ->IsWasm()) {  // TODO(paolosev) - Skip not-Wasm stack frames?
      uint64_t script_id = stack_frame->GetScriptId();
      uint64_t offset = stack_frame->GetColumn() - 1;  // Column is 1-based.
      uint64_t address = script_id << 32 | offset;
      call_stack.push_back(address);
    }
  }
  return call_stack;
}

void GdbServer::DebugDelegate::BreakProgramRequested(
    Local<v8::Context> paused_context,
    const std::vector<debug::BreakpointId>& inspector_break_points_hit) {
  Handle<FixedArray> stack_trace = isolate_->CaptureCurrentStackTrace(
      kMaxWasmCallStack, v8::StackTrace::kOverview);
  std::vector<uint64_t> call_stack =
      CalculateCallStack(Utils::StackTraceToLocal(stack_trace));
  gdb_server_->thread_->OnSuspended(call_stack);
  gdb_server_->runMessageLoopOnPause();
}

void GdbServer::DebugDelegate::ExceptionThrown(
    Local<v8::Context> paused_context, Local<Value> exception,
    Local<Value> promise, bool is_uncaught,
    debug::ExceptionType exception_type) {
  if (exception_type == v8::debug::kException && is_uncaught) {
    v8::Local<v8::StackTrace> stack_trace =
        debug::GetDetailedStackTrace(reinterpret_cast<v8::Isolate*>(isolate_),
                                     v8::Local<v8::Object>::Cast(exception));
    std::vector<uint64_t> call_stack = CalculateCallStack(stack_trace);
    gdb_server_->thread_->OnException(call_stack);
    gdb_server_->runMessageLoopOnPause();
  }
}

bool GdbServer::DebugDelegate::IsFunctionBlackboxed(
    Local<debug::Script> script, const debug::Location& start,
    const debug::Location& end) {
  return false;
}

std::string GdbServer::getWasmModuleString() {
  base::MutexGuard guard(&mutex_);
  std::string result("l<library-list>");
  for (const auto& pair : scripts_) {
    uint64_t module_id = pair.first;
    uint64_t address = module_id << 32;

    const WasmDebugScript* wasm_debug_script = pair.second.get();
    v8::Local<debug::WasmScript> wasm_script =
        wasm_debug_script->wasm_script_.Get(wasm_debug_script->isolate_);
    v8::Local<v8::String> name;
    std::string module_name;
    if (wasm_script->Name().ToLocal(&name)) {
      module_name = *(v8::String::Utf8Value(wasm_debug_script->isolate_, name));
    }

    char address_string[32];
    snprintf(address_string, sizeof(address_string), "%" PRIu64, address);
    result += "<library name=\"";
    result += module_name;
    result += "\"><section address=\"";
    result += address_string;
    result += "\"/></library>";
  }
  result += "</library-list>";
  return result;
}

bool GdbServer::getWasmGlobal(uint32_t module_id, uint32_t index,
                              uint64_t* value) {
  base::MutexGuard guard(&mutex_);
  bool result = false;
  RunSyncTask(task_runner_.get(), [this, &result, &module_id, &index,
                                   &value]() {
    ScriptsMap::iterator scriptIterator = scripts_.find(module_id);
    if (scriptIterator == scripts_.end()) {
      result = false;
    } else {
      const WasmDebugScript* wasm_debug_script = scriptIterator->second.get();
      v8::Local<debug::WasmScript> wasm_script =
          wasm_debug_script->wasm_script_.Get(wasm_debug_script->isolate_);
      result = wasm_script->GetWasmGlobal(index, value);
    }
  });
  return result;
}

bool GdbServer::getWasmLocal(uint32_t module_id, uint32_t frame_index,
                             uint32_t index, uint64_t* value) {
  base::MutexGuard guard(&mutex_);
  bool result = false;
  RunSyncTask(task_runner_.get(), [this, &result, &module_id, &frame_index,
                                   &index, &value]() {
    // Convert the 'global' frame_index into a frame index specific to the
    // corresponding InterpreterHandle.
    ScriptsMap::iterator scriptIterator = scripts_.find(module_id);
    if (scriptIterator == scripts_.end()) {
      result = false;
    } else {
      std::vector<uint64_t> call_stack;
      if (getWasmCallStack(&call_stack) && frame_index < call_stack.size()) {
        uint32_t interpreter_frame_index = 0;
        uint32_t i = 0;
        uint32_t current_module_id = call_stack[i++] >> 32;
        while (index < frame_index) {
          uint32_t module_id = call_stack[i++] >> 32;
          if (current_module_id == module_id) {
            interpreter_frame_index++;
          } else {
            current_module_id = module_id;
            interpreter_frame_index = 0;
          }
        }

        const WasmDebugScript* wasm_debug_script = scriptIterator->second.get();
        v8::Local<debug::WasmScript> wasm_script =
            wasm_debug_script->wasm_script_.Get(wasm_debug_script->isolate_);
        result =
            wasm_script->GetWasmLocal(interpreter_frame_index, index, value);
      }
    }
  });
  return result;
}

bool GdbServer::getWasmOperandStackValue(uint32_t module_id, uint32_t index,
                                         uint64_t* value) {
  base::MutexGuard guard(&mutex_);
  bool result = false;
  RunSyncTask(task_runner_.get(), [this, &result, &module_id, &index,
                                   &value]() {
    ScriptsMap::iterator scriptIterator = scripts_.find(module_id);
    if (scriptIterator == scripts_.end()) {
      result = false;
    } else {
      const WasmDebugScript* wasm_debug_script = scriptIterator->second.get();
      v8::Local<debug::WasmScript> wasm_script =
          wasm_debug_script->wasm_script_.Get(wasm_debug_script->isolate_);
      result = wasm_script->GetWasmOperandStackValue(index, value);
    }
  });
  return result;
}

uint32_t GdbServer::getWasmMemory(uint32_t module_id, uint32_t offset,
                                  uint8_t* buffer, uint32_t size) {
  base::MutexGuard guard(&mutex_);
  uint32_t result = 0;
  RunSyncTask(task_runner_.get(), [this, &module_id, &result, &offset, &buffer,
                                   &size]() {
    ScriptsMap::iterator scriptIterator = scripts_.find(module_id);
    if (scriptIterator != scripts_.end()) {
      const WasmDebugScript* wasm_debug_script = scriptIterator->second.get();
      v8::Local<debug::WasmScript> wasm_script =
          wasm_debug_script->wasm_script_.Get(wasm_debug_script->isolate_);
      result = wasm_script->GetWasmMemory(offset, buffer, size);
    }
  });
  return result;
}

bool GdbServer::getWasmCallStack(std::vector<uint64_t>* callStackPCs) {
  *callStackPCs = thread_->get_call_stack();
  return true;
}

uint32_t GdbServer::getWasmModuleBytes(uint64_t address, uint8_t* buffer,
                                       uint32_t size) {
  base::MutexGuard guard(&mutex_);
  uint32_t result = 0;
  RunSyncTask(task_runner_.get(), [this, &result, &address, &buffer, &size]() {
    uint32_t module_id = address >> 32;
    uint32_t offset = address & 0xffffffff;

    ScriptsMap::iterator scriptIterator = scripts_.find(module_id);
    if (scriptIterator != scripts_.end()) {
      const WasmDebugScript* wasm_debug_script = scriptIterator->second.get();
      v8::Local<debug::WasmScript> wasm_script =
          wasm_debug_script->wasm_script_.Get(wasm_debug_script->isolate_);
      result = wasm_script->GetWasmModuleBytes(offset, buffer, size);
    }
  });
  return result;
}

bool GdbServer::addBreakpoint(uint64_t address) {
  base::MutexGuard guard(&mutex_);
  bool result = false;
  RunSyncTask(task_runner_.get(), [this, &result, address]() {
    uint32_t module_id = address >> 32;
    uint32_t offset = address & 0xffffffff;

    ScriptsMap::iterator scriptIterator = scripts_.find(module_id);
    if (scriptIterator == scripts_.end()) {
      result = false;
    } else {
      int breakpoint_id = 0;
      const WasmDebugScript* wasm_debug_script = scriptIterator->second.get();
      v8::Local<debug::WasmScript> wasm_script =
          wasm_debug_script->wasm_script_.Get(wasm_debug_script->isolate_);
      result = wasm_script->AddWasmBreakpoint(offset, &breakpoint_id);
      if (result) {
        breakpoints_[address] = breakpoint_id;
      }
    }
  });
  return result;
}

bool GdbServer::removeBreakpoint(uint64_t address) {
  base::MutexGuard guard(&mutex_);
  BreakpointsMap::iterator it = breakpoints_.find(address);
  if (it == breakpoints_.end()) {
    return false;
  }
  int breakpoint_id = it->second;
  breakpoints_.erase(it);

  RunSyncTask(task_runner_.get(), [this, &address, &breakpoint_id]() {
    uint32_t module_id = address >> 32;
    uint32_t offset = address & 0xffffffff;

    ScriptsMap::iterator scriptIterator = scripts_.find(module_id);
    if (scriptIterator != scripts_.end()) {
      const WasmDebugScript* wasm_debug_script = scriptIterator->second.get();
      v8::Local<debug::WasmScript> wasm_script =
          wasm_debug_script->wasm_script_.Get(wasm_debug_script->isolate_);
      wasm_script->RemoveWasmBreakpoint(offset, breakpoint_id);
    }
  });

  return true;
}

// static
int GdbServer::getSessionMessageId() {
  static int s_message_id = 1;
  return s_message_id++;
}

void GdbServer::suspend() {
  if (wasm_engine_) {
    wasm_engine_->Suspend();
  }
}

void GdbServer::prepareStep() {
  base::MutexGuard guard(&mutex_);

  // TODO(paolosev) - Currently this only works if we only have one isolate.
  ScriptsMap::iterator scriptIterator = scripts_.begin();
  if (scriptIterator != scripts_.end()) {
    const WasmDebugScript* wasm_debug_script = scriptIterator->second.get();
    i::Isolate* isolate =
        reinterpret_cast<i::Isolate*>(wasm_debug_script->isolate_);
    DebugScope debug_scope(isolate->debug());
    debug::PrepareStep(wasm_debug_script->isolate_,
                       debug::StepAction::StepNext);
  }
}

void GdbServer::runMessageLoopOnPause() { task_runner_->RunMessageLoop(true); }

void GdbServer::quitMessageLoopOnPause() {
  task_runner_->QuitMessageLoop();
  task_runner_->Terminate();
}

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
