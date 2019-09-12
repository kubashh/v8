// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/gdb-server/gdb-server.h"
#include "src/inspector/gdb-server/gdb-server-thread.h"

#include "src/inspector/v8-debugger-agent-impl.h"
#include "src/inspector/v8-inspector-session-impl.h"
#include "src/utils/locked-queue-inl.h"

namespace v8_inspector {

enum { kModuleEmbedderDataIndex, kInspectorClientIndex };

class _TaskRunner {
 public:
  class Task {
   public:
    virtual ~Task() = default;
    virtual bool is_priority_task() = 0;
    virtual void Run() = 0;
  };

  _TaskRunner();

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
  v8::internal::LockedQueue<Task*> queue_;
  v8::internal::LockedQueue<Task*> deferred_queue_;
  v8::base::Semaphore process_queue_semaphore_;

  int nested_loop_count_;

  std::atomic<int> is_terminated_;

  DISALLOW_COPY_AND_ASSIGN(_TaskRunner);
};

_TaskRunner::_TaskRunner()
    : process_queue_semaphore_(0), nested_loop_count_(0), is_terminated_(0) {}

void _TaskRunner::RunMessageLoop(bool only_protocol) {
  is_terminated_ = 0;
  int loop_number = ++nested_loop_count_;
  while (nested_loop_count_ == loop_number && !is_terminated_) {
    _TaskRunner::Task* task = GetNext(only_protocol);
    if (!task) return;
    task->Run();
    delete task;
  }
}

void _TaskRunner::QuitMessageLoop() {
  DCHECK_LT(0, nested_loop_count_);
  --nested_loop_count_;
}

void _TaskRunner::Append(Task* task) {
  queue_.Enqueue(task);
  process_queue_semaphore_.Signal();
}

void _TaskRunner::Terminate() {
  is_terminated_++;
  process_queue_semaphore_.Signal();
}

_TaskRunner::Task* _TaskRunner::GetNext(bool only_protocol) {
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
void RunSyncTask(_TaskRunner* task_runner, T callback) {
  class SyncTask : public _TaskRunner::Task {
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

class InspectorFrontend : public V8Inspector::Channel {
 public:
  explicit InspectorFrontend(v8::Local<v8::Context> context) {
    isolate_ = context->GetIsolate();
    context_.Reset(isolate_, context);
  }
  ~InspectorFrontend() override = default;

 private:
  void sendResponse(int callId,
                    std::unique_ptr<StringBuffer> message) override {
    Send(message->string());
  }
  void sendNotification(std::unique_ptr<StringBuffer> message) override {
    Send(message->string());
  }
  void flushProtocolNotifications() override {}

  void Send(const StringView& string) {
    // ignore responses from backend
  }

  v8::Isolate* isolate_;
  v8::Global<v8::Context> context_;
};

class InspectorClient : public V8InspectorClient {
 public:
  InspectorClient(v8::Local<v8::Context> context, bool connect) {
    if (!connect) return;
    isolate_ = context->GetIsolate();
    channel_.reset(new InspectorFrontend(context));
    inspector_ = V8Inspector::create(isolate_, this);
    session_ = inspector_->connect(1, channel_.get(), StringView());
    context->SetAlignedPointerInEmbedderData(kInspectorClientIndex, this);
    inspector_->contextCreated(
        V8ContextInfo(context, kContextGroupId, StringView()));

    context_.Reset(isolate_, context);

    task_runner_ = std::make_unique<_TaskRunner>();
  }

  _TaskRunner* GetTaskRunner() const { return task_runner_.get(); }

  static V8InspectorSession* GetSession(v8::Local<v8::Context> context) {
    InspectorClient* inspector_client = static_cast<InspectorClient*>(
        context->GetAlignedPointerFromEmbedderData(kInspectorClientIndex));
    return inspector_client->session_.get();
  }

  void SendMessage(v8::Isolate* isolate, const char* message) {
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    V8InspectorSession* session = InspectorClient::GetSession(context);
    size_t length = strlen(message);
    StringView message_view(reinterpret_cast<const uint8_t*>(message), length);
    {
      v8::SealHandleScope seal_handle_scope(isolate);
      session->dispatchProtocolMessage(message_view);
    }
  }

  void runMessageLoopOnPause(int contextGroupId) override {
    task_runner_->RunMessageLoop(true);
  }
  void quitMessageLoopOnPause() override {
    task_runner_->QuitMessageLoop();
    task_runner_->Terminate();
  }
  void runIfWaitingForDebugger(int contextGroupId) override {}

 private:
  v8::Local<v8::Context> ensureDefaultContextInGroup(int group_id) override {
    DCHECK(isolate_);
    DCHECK_EQ(kContextGroupId, group_id);
    return context_.Get(isolate_);
  }

  static void SendInspectorMessage(
      const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    args.GetReturnValue().Set(Undefined(isolate));
    v8::Local<v8::String> message = args[0]->ToString(context).ToLocalChecked();
    V8InspectorSession* session = InspectorClient::GetSession(context);
    int length = message->Length();
    std::unique_ptr<uint16_t[]> buffer(new uint16_t[length]);
    message->Write(isolate, buffer.get(), 0, length);
    StringView message_view(buffer.get(), length);
    {
      v8::SealHandleScope seal_handle_scope(isolate);
      session->dispatchProtocolMessage(message_view);
    }
    args.GetReturnValue().Set(True(isolate));
  }

  static const int kContextGroupId = 1;

  std::unique_ptr<_TaskRunner> task_runner_;
  std::unique_ptr<V8Inspector> inspector_;
  std::unique_ptr<V8InspectorSession> session_;
  std::unique_ptr<V8Inspector::Channel> channel_;
  v8::Global<v8::Context> context_;
  v8::Isolate* isolate_;
};

GdbServer::GdbServer(v8::Isolate* isolate) {
  isolate_ = reinterpret_cast<v8::Isolate*>(isolate);
  auto context = isolate_->GetCurrentContext();

  inspector_client_ = new InspectorClient(context, true);
  session_.reset(inspector_client_->GetSession(context));

  thread_ = std::make_unique<GdbServerThread>(this, inspector_client_,
                                              session_.get());
  if (!thread_->Start()) {
    // ???
    thread_ = nullptr;
    return;
  }

  std::string msg =
      "{"
      "\"id\":" +
      std::to_string(getSessionMessageId()) + "," +
      "\"method\":\"Debugger.enable\","
      "\"params\":{}"
      "}";
  session_->dispatchProtocolMessage(v8_inspector::StringView(
      reinterpret_cast<const uint8_t*>(msg.c_str()), msg.length()));

  V8DebuggerAgentImpl* debuggerAgent =
      (reinterpret_cast<V8InspectorSessionImpl*>(session_.get()))
          ->debuggerAgent();
  debuggerAgent->setGdbServer(this);
}

GdbServer::~GdbServer() {
  if (thread_) {
    thread_->Join();
    thread_ = nullptr;
  }

  delete inspector_client_;
  inspector_client_ = nullptr;
}

void GdbServer::quitMessageLoopOnPause() {
  inspector_client_->quitMessageLoopOnPause();
}

v8::Isolate* GdbServer::isolate() const {
  V8DebuggerAgentImpl* debuggerAgent =
      (reinterpret_cast<V8InspectorSessionImpl*>(session_.get()))
          ->debuggerAgent();
  DCHECK(isolate_ == debuggerAgent->isolate());

  return isolate_;
}

void GdbServer::onWasmModuleAdded(int moduleId, uint32_t codeOffset,
                                  const std::string& moduleName,
                                  const std::string& sourceMappingURL) {
  modules_[moduleId] =
      Module{moduleId, codeOffset, moduleName, sourceMappingURL};
}

void GdbServer::onPaused(const std::vector<uint64_t>& callFrames) {
  thread_->OnPaused(callFrames);
}

std::string GdbServer::getWasmModuleString() const {
  std::string result("l<library-list>");
  for (const auto pair : modules_) {
    uint64_t module_id = pair.first;
    uint64_t address = module_id << 32;
    char address_string[32];
    snprintf(address_string, sizeof(address_string), "%llu",
             address + pair.second.code_offset_);
    result += "<library name=\"";
    result += pair.second.module_name_;
    result += "\"><section address=\"";
    result += address_string;
    result += "\"/></library>";
  }
  result += "</library-list>";
  return result;
}

bool GdbServer::getWasmGlobal(uint32_t wasmModuleId, uint32_t index,
                              uint64_t* value) {
  bool result = false;
  RunSyncTask(inspector_client_->GetTaskRunner(), [this, &result, &wasmModuleId,
                                                   &index, &value]() {
    V8DebuggerAgentImpl* debuggerAgent =
        (reinterpret_cast<V8InspectorSessionImpl*>(session_.get()))
            ->debuggerAgent();
    result = debuggerAgent->getWasmGlobal(wasmModuleId, index, value);
  });
  return result;
}

bool GdbServer::getWasmLocal(uint32_t wasmModuleId, uint32_t index,
                             uint64_t* value) {
  bool result = false;
  RunSyncTask(inspector_client_->GetTaskRunner(), [this, &result, &wasmModuleId,
                                                   &index, &value]() {
    V8DebuggerAgentImpl* debuggerAgent =
        (reinterpret_cast<V8InspectorSessionImpl*>(session_.get()))
            ->debuggerAgent();
    result = debuggerAgent->getWasmLocal(wasmModuleId, index, value);
  });
  return result;
}

bool GdbServer::getWasmStackValue(uint32_t wasmModuleId, uint32_t index,
                                  uint64_t* value) {
  bool result = false;
  RunSyncTask(inspector_client_->GetTaskRunner(), [this, &result, &wasmModuleId,
                                                   &index, &value]() {
    V8DebuggerAgentImpl* debuggerAgent =
        (reinterpret_cast<V8InspectorSessionImpl*>(session_.get()))
            ->debuggerAgent();
    result = debuggerAgent->getWasmStackValue(wasmModuleId, index, value);
  });
  return result;
}

bool GdbServer::getWasmMemory(uint32_t offset, uint8_t* buffer, uint32_t size) {
  bool result = false;
  RunSyncTask(inspector_client_->GetTaskRunner(),
              [this, &result, &offset, &buffer, &size]() {
                V8DebuggerAgentImpl* debuggerAgent =
                    (reinterpret_cast<V8InspectorSessionImpl*>(session_.get()))
                        ->debuggerAgent();
                result = debuggerAgent->getWasmMemory(offset, buffer, size);
              });
  return result;
}

bool GdbServer::getWasmCallStack(std::vector<uint64_t>* callStackPCs) {
  bool result = false;
  RunSyncTask(inspector_client_->GetTaskRunner(),
              [this, &result, &callStackPCs]() {
                V8DebuggerAgentImpl* debuggerAgent =
                    (reinterpret_cast<V8InspectorSessionImpl*>(session_.get()))
                        ->debuggerAgent();
                result = debuggerAgent->getWasmCallStack(callStackPCs);
              });
  return result;
}

bool GdbServer::addBreakpoint(uint64_t address) {
  bool result = false;
  RunSyncTask(inspector_client_->GetTaskRunner(), [this, &result, address]() {
    uint32_t moduleId = address >> 32;
    uint32_t offset = address & 0xffffffff;
    V8DebuggerAgentImpl* debuggerAgent =
        (reinterpret_cast<V8InspectorSessionImpl*>(session_.get()))
            ->debuggerAgent();
    result = debuggerAgent->addWasmBreakpoint(moduleId, offset);
  });
  return result;
}

bool GdbServer::removeBreakpoint(uint64_t address) {
  bool result = false;
  RunSyncTask(inspector_client_->GetTaskRunner(), [this, &result, address]() {
    uint32_t moduleId = address >> 32;
    uint32_t offset = address & 0xffffffff;
    V8DebuggerAgentImpl* debuggerAgent =
        (reinterpret_cast<V8InspectorSessionImpl*>(session_.get()))
            ->debuggerAgent();
    result = debuggerAgent->removeWasmBreakpoint(moduleId, offset);
  });
  return result;
}

void GdbServer::step() {
  RunSyncTask(inspector_client_->GetTaskRunner(), [this]() {
    std::string msg =
        "{"
        "\"id\":" +
        std::to_string(getSessionMessageId()) + "," +
        "\"method\":\"Debugger.stepInto\","
        "\"params\":{}"
        "}";
    session_->dispatchProtocolMessage(v8_inspector::StringView(
        reinterpret_cast<const uint8_t*>(msg.c_str()), msg.length()));
  });
}

// static
int GdbServer::getSessionMessageId() {
  static int s_message_id = 1;
  return s_message_id++;
}

void GdbServer::sendPauseRequest() {
  // TODO(paolosev)
  // Not sure what is a good way to suspend a WASM module which is running
  // in 'compiled' mode. The only way that seems to work is by setting
  // breakpoints at the beginning of each function in the module.
  // But there is not even seem to be a way to remove those breakpoints.

  // This is very temporary code, used just to put the script in pause when we
  // attach with LLDB and manually test the debugging functionalities, but
  // obviously this needs to be completely refactored.
  addInitialBreakpoints();
}

void GdbServer::addInitialBreakpoints() {
  if (!modules_.empty()) {
    int module_id = modules_.begin()->first;
    v8_inspector::V8DebuggerAgentImpl* debuggerAgent =
        (reinterpret_cast<V8InspectorSessionImpl*>(session_.get()))
            ->debuggerAgent();
    std::vector<int> functionsOffsets =
        debuggerAgent->getWasmFunctionsOffsets(module_id);

    for (int offset : functionsOffsets) {
      if (offset == 0) continue;
      std::string msg =
          "{"
          "\"id\":" +
          std::to_string(getSessionMessageId()) + "," +
          "\"method\":\"Debugger.setBreakpoint\","
          "\"params\":{"
          "\"location\":{"
          "\"scriptId\":\"" +
          std::to_string(module_id) + "\"," +
          "\"lineNumber\":0,"
          "\"columnNumber\":" +
          std::to_string(offset) +
          "}"
          "}"
          "}";
      session_->dispatchProtocolMessage(v8_inspector::StringView(
          reinterpret_cast<const uint8_t*>(msg.c_str()), msg.length()));
      break;
    }
  }
}

void GdbServer::removeInitialBreakpoints() {
  v8_inspector::V8DebuggerAgentImpl* debuggerAgent =
      (reinterpret_cast<V8InspectorSessionImpl*>(session_.get()))
          ->debuggerAgent();

  // This does not work, it does not really removes breakpoints from
  // WasmModuleObject...
  debuggerAgent->removeAllBreakpoints();
}

}  // namespace v8_inspector
