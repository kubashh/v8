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
#include "src/wasm/wasm-value.h"

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
  base::LockGuard<base::RecursiveMutex> guard(&mutex_);
  if (isolate_delegates_.find(isolate) == isolate_delegates_.end()) {
    isolate_delegates_[isolate] =
        std::make_unique<DebugDelegate>(isolate, this);
  }
}

void GdbServer::RemoveIsolate(Isolate* isolate) {
  base::LockGuard<base::RecursiveMutex> guard(&mutex_);
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

void GdbServer::Suspend() {
  if (wasm_engine_) {
    wasm_engine_->Suspend();
  }
}

void GdbServer::PrepareStep() {
  base::LockGuard<base::RecursiveMutex> guard(&mutex_);

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

std::string GdbServer::GetWasmModuleString() {
  base::LockGuard<base::RecursiveMutex> guard(&mutex_);
  std::string result("l<library-list>");
  for (const auto& pair : scripts_) {
    uint32_t module_id = pair.first;
    uint64_t address = WasmAddressFromModuleAndOffset(module_id, 0);

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

bool WasmValueToUint64(const wasm::WasmValue& wasm_value, uint64_t* value) {
  switch (wasm_value.type()) {
    case wasm::kWasmI32:
      *value = wasm_value.to_i32();
      return true;
    case wasm::kWasmI64:
      *value = wasm_value.to_i64();
      return true;
    case wasm::kWasmF32:  // TODO(paolosev)
      *value = wasm_value.to_f32();
      return true;
    case wasm::kWasmF64:  // TODO(paolosev)
      *value = wasm_value.to_f64();
      return true;
    case wasm::kWasmS128:
    case wasm::kWasmAnyRef:
    default:
      // Not supported    // TODO(paolosev)
      return false;
  }
}

v8::Local<debug::WasmScript> GdbServer::GetWasmScript(uint32_t module_id) {
  base::LockGuard<base::RecursiveMutex> guard(&mutex_);

  ScriptsMap::const_iterator scriptIterator = scripts_.find(module_id);
  if (scriptIterator != scripts_.end()) {
    const WasmDebugScript* wasm_debug_script = scriptIterator->second.get();
    return wasm_debug_script->wasm_script_.Get(wasm_debug_script->isolate_);
  }
  return v8::Local<debug::WasmScript>();
}

Handle<WasmInstanceObject> GdbServer::GetWasmInstance(
    v8::Local<debug::WasmScript> wasm_script) {
  if (!wasm_script.IsEmpty()) {
    Handle<Script> script = Utils::OpenHandle(*wasm_script);
    DCHECK_EQ(Script::TYPE_WASM, script->type());
    Isolate* isolate = script->GetIsolate();

    // Only uses the first instance of this module.
    Handle<WeakArrayList> weak_instance_list(script->wasm_weak_instance_list(),
                                             isolate);
    if (weak_instance_list->length() > 0) {
      MaybeObject maybe_instance = weak_instance_list->Get(0);
      if (maybe_instance->IsWeak()) {
        Handle<WasmInstanceObject> instance(
            WasmInstanceObject::cast(maybe_instance->GetHeapObjectAssumeWeak()),
            isolate);
        return instance;
      }
    }
  }
  return Handle<WasmInstanceObject>::null();
}

bool GdbServer::GetWasmGlobal(uint32_t module_id, uint32_t index,
                              uint64_t* value) {
  bool result = false;
  RunSyncTask(task_runner_.get(), [this, &result, module_id, index, value]() {
    DisallowHeapAllocation no_gc;
    v8::Local<debug::WasmScript> wasm_script =
        GdbServer::GetWasmScript(module_id);
    Handle<WasmInstanceObject> instance = GetWasmInstance(wasm_script);
    if (!instance.is_null()) {
      i::Isolate* isolate =
          reinterpret_cast<i::Isolate*>(wasm_script->GetIsolate());
      Handle<WasmModuleObject> module_object(instance->module_object(),
                                             isolate);
      const wasm::WasmModule* module = module_object->module();
      if (index < module->globals.size()) {
        wasm::WasmValue wasm_value = WasmInstanceObject::GetGlobalValue(
            instance, module->globals[index]);
        result = WasmValueToUint64(wasm_value, value);
      }
    }
  });
  return result;
}

bool GdbServer::GetWasmLocal(uint32_t module_id, uint32_t frame_index,
                             uint32_t index, uint64_t* value) {
  bool result = false;
  RunSyncTask(task_runner_.get(), [this, &result, module_id, frame_index, index,
                                   value]() {
    v8::Local<debug::WasmScript> wasm_script =
        GdbServer::GetWasmScript(module_id);
    Handle<WasmInstanceObject> instance = GetWasmInstance(wasm_script);
    if (!instance.is_null()) {
      // Convert the 'global' frame_index into a frame index specific to the
      // corresponding InterpreterHandle.
      std::vector<uint64_t> call_stack;
      GetWasmCallStack(&call_stack);
      if (frame_index < call_stack.size()) {
        uint32_t interpreter_frame_index = 0;
        uint32_t i = 0;
        uint32_t current_module_id = ModuleIdFromWasmAddress(call_stack[i++]);
        while (i <= frame_index) {
          uint32_t module_id = ModuleIdFromWasmAddress(call_stack[i++]);
          if (current_module_id == module_id) {
            interpreter_frame_index++;
          } else {
            current_module_id = module_id;
            interpreter_frame_index = 0;
          }
        }

        Handle<WasmDebugInfo> debug_info =
            WasmInstanceObject::GetOrCreateDebugInfo(instance);
        wasm::WasmValue wasm_value;
        if (WasmDebugInfo::GetWasmLocal(debug_info, interpreter_frame_index,
                                        index, &wasm_value)) {
          result = WasmValueToUint64(wasm_value, value);
        }
      }
    }
  });
  return result;
}

bool GdbServer::GetWasmOperandStackValue(uint32_t module_id, uint32_t index,
                                         uint64_t* value) {
  return false;  // TODO(paolosev) - not used?
}

uint32_t GdbServer::GetWasmMemory(uint32_t module_id, uint32_t offset,
                                  uint8_t* buffer, uint32_t size) {
  uint32_t bytes_read = 0;
  RunSyncTask(task_runner_.get(), [this, module_id, offset, buffer, size,
                                   &bytes_read]() {
    DisallowHeapAllocation no_gc;
    v8::Local<debug::WasmScript> wasm_script =
        GdbServer::GetWasmScript(module_id);
    Handle<WasmInstanceObject> instance = GetWasmInstance(wasm_script);
    if (!instance.is_null()) {
      uint8_t* mem_start = instance->memory_start();
      size_t mem_size = instance->memory_size();
      if (static_cast<uint64_t>(offset) + size <= mem_size) {
        memcpy(buffer, mem_start + offset, size);
        bytes_read = size;
      } else if (offset < mem_size) {
        bytes_read = static_cast<uint32_t>(mem_size) - offset;
        memcpy(buffer, mem_start + offset, bytes_read);
      }
    }
  });
  return bytes_read;
}

uint32_t GdbServer::GetWasmModuleBytes(uint64_t address, uint8_t* buffer,
                                       uint32_t size) {
  uint32_t bytes_read = 0;
  RunSyncTask(task_runner_.get(), [this, address, buffer, size, &bytes_read]() {
    DisallowHeapAllocation no_gc;

    uint32_t module_id = 0;
    uint32_t offset = 0;
    ModuleIdAndOffsetFromWasmAddress(address, &module_id, &offset);

    v8::Local<debug::WasmScript> wasm_script =
        GdbServer::GetWasmScript(module_id);
    Handle<WasmInstanceObject> instance = GetWasmInstance(wasm_script);
    if (!instance.is_null()) {
      i::Isolate* isolate =
          reinterpret_cast<i::Isolate*>(wasm_script->GetIsolate());
      Handle<WasmModuleObject> module_object(instance->module_object(),
                                             isolate);
      wasm::NativeModule* native_module = module_object->native_module();
      const wasm::ModuleWireBytes wire_bytes(native_module->wire_bytes());
      if (offset < wire_bytes.length()) {
        uint32_t module_size = static_cast<uint32_t>(wire_bytes.length());
        bytes_read = module_size - offset >= size ? size : module_size - offset;
        memcpy(buffer, wire_bytes.start() + offset, bytes_read);
      }
    }
  });
  return bytes_read;
}

bool GdbServer::AddBreakpoint(uint64_t address) {
  bool result = false;
  RunSyncTask(task_runner_.get(), [this, &result, address]() {
    uint32_t module_id = 0;
    uint32_t offset = 0;
    ModuleIdAndOffsetFromWasmAddress(address, &module_id, &offset);

    v8::Local<debug::WasmScript> wasm_script = GetWasmScript(module_id);
    if (!wasm_script.IsEmpty()) {
      Handle<Script> script = Utils::OpenHandle(*wasm_script);
      DCHECK_EQ(Script::TYPE_WASM, script->type());
      Isolate* isolate = script->GetIsolate();
      Handle<String> condition = isolate->factory()->empty_string();
      int breakpoint_id = 0;
      int breakpoint_address = static_cast<int>(offset);
      if (isolate->debug()->SetBreakPointForScript(
              script, condition, &breakpoint_address, &breakpoint_id)) {
        breakpoints_[address] = breakpoint_id;
        result = true;
      }
    }
  });
  return result;
}

bool GdbServer::RemoveBreakpoint(uint64_t address) {
  bool result = false;
  RunSyncTask(task_runner_.get(), [this, address, &result]() {
    BreakpointsMap::iterator it = breakpoints_.find(address);
    if (it != breakpoints_.end()) {
      int breakpoint_id = it->second;
      breakpoints_.erase(it);

      uint32_t module_id = 0;
      uint32_t offset = 0;
      ModuleIdAndOffsetFromWasmAddress(address, &module_id, &offset);

      v8::Local<debug::WasmScript> wasm_script = GetWasmScript(module_id);
      if (!wasm_script.IsEmpty()) {
        i::Handle<i::Script> script = Utils::OpenHandle(*wasm_script);
        DCHECK_EQ(i::Script::TYPE_WASM, script->type());
        i::Isolate* isolate = script->GetIsolate();
        isolate->debug()->RemoveWasmBreakpoint(script, offset, breakpoint_id);
        result = true;
      }
    }
  });
  return result;
}

void GdbServer::GetWasmCallStack(std::vector<uint64_t>* callStackPCs) {
  *callStackPCs = thread_->get_call_stack();
}

void GdbServer::RunMessageLoopOnPause() { task_runner_->RunMessageLoop(true); }

void GdbServer::QuitMessageLoopOnPause() {
  task_runner_->QuitMessageLoop();
  task_runner_->Terminate();
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
  if (script->IsWasm()) {
    base::LockGuard<base::RecursiveMutex> guard(&gdb_server_->mutex_);

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
      uint32_t script_id = stack_frame->GetScriptId();
      uint32_t offset = stack_frame->GetColumn() - 1;  // Column is 1-based.
      uint64_t address = WasmAddressFromModuleAndOffset(script_id, offset);
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
  gdb_server_->RunMessageLoopOnPause();
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
    gdb_server_->RunMessageLoopOnPause();
  }
}

bool GdbServer::DebugDelegate::IsFunctionBlackboxed(
    Local<debug::Script> script, const debug::Location& start,
    const debug::Location& end) {
  return false;
}

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
