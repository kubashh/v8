// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/gdb-server/gdb-server.h"

#include "src/wasm/gdb-server/gdb-server-thread.h"
#include "src/wasm/wasm-engine.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

static const uint32_t kMaxWasmCallStack = 20;

bool GdbServer::Initialize() {
  DCHECK(FLAG_wasm_gdb_remote);
  DCHECK(!thread_);

  thread_ = std::make_unique<GdbServerThread>(this);
  if (!thread_->StartAndInitialize()) {
    TRACE_GDB_REMOTE(
        "Cannot initialize thread, GDB-remote debugging will be disabled.\n");
    thread_ = nullptr;
    return false;
  }
  return true;
}

void GdbServer::Shutdown() {
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
    isolate_delegates_.erase(it);
  }
}

void GdbServer::AddWasmModule(Local<debug::WasmScript> wasm_script) {
  base::LockGuard<base::RecursiveMutex> guard(&mutex_);
  v8::Isolate* isolate = wasm_script->GetIsolate();
  scripts_[wasm_script->Id()] =
      std::make_unique<WasmDebugScript>(WasmDebugScript(isolate, wasm_script));
}

GdbServer::DebugDelegate::DebugDelegate(Isolate* isolate, GdbServer* gdb_server)
    : isolate_(isolate), gdb_server_(gdb_server) {
  isolate_->SetCaptureStackTraceForUncaughtExceptions(
      true, kMaxWasmCallStack, v8::StackTrace::kOverview);

  // Register the delegate
  isolate_->debug()->SetDebugDelegate(this);
}

void GdbServer::DebugDelegate::ScriptCompiled(Local<debug::Script> script,
                                              bool is_live_edited,
                                              bool has_compile_error) {
  if (script->IsWasm()) {
    v8::Isolate* isolate = script->GetIsolate();
    DCHECK_EQ(reinterpret_cast<v8::Isolate*>(isolate_), isolate);
    gdb_server_->AddWasmModule(script.As<debug::WasmScript>());
  }
}

void GdbServer::DebugDelegate::BreakProgramRequested(
    Local<v8::Context> paused_context,
    const std::vector<debug::BreakpointId>& inspector_break_points_hit) {}

void GdbServer::DebugDelegate::ExceptionThrown(
    Local<v8::Context> paused_context, Local<Value> exception,
    Local<Value> promise, bool is_uncaught,
    debug::ExceptionType exception_type) {}

bool GdbServer::DebugDelegate::IsFunctionBlackboxed(
    Local<debug::Script> script, const debug::Location& start,
    const debug::Location& end) {
  return false;
}

GdbServer::WasmDebugScript::WasmDebugScript(
    v8::Isolate* isolate, Local<debug::WasmScript> wasm_script) {
  isolate_ = isolate;
  wasm_script_ = Global<debug::WasmScript>(isolate, wasm_script);
}

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8
