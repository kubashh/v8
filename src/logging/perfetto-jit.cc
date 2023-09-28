// Copyright 2023 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/logging/perfetto-jit.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "perfetto/base/time.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/trace/chrome/v8.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"
#include "protos/perfetto/trace/track_event/thread_descriptor.pbzero.h"
#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/logging/code-events.h"
#include "src/logging/log.h"
#include "src/objects/abstract-code.h"
#include "src/objects/heap-object.h"
#include "src/objects/name.h"
#include "src/objects/objects-inl.h"
#include "src/objects/script.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/tagged.h"
#include "v8-function.h"

/*
WIP. This code is a ugly hack at best.

Random thoughts:
 * Add a TRACE_CODE macro to instrument the tracepoints and bypass all the
   LogEventListener stuff
 * We should re-emit all the code when a producer connects (or have an option in
   the config)
 * Not sure what the difference between Tagged<> and Handle<> is. Make sure I
   use the right one here
 * Are all the `DisallowGarbageCollection no_gc;` lines really needed? What does
   this do? Code calling into LogEventListener seems to already put this on the
   stack.
*/

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(v8::internal::JitDataSource,
                                           v8::internal::JitDataSourceTraits);

namespace v8 {
namespace internal {
namespace {

using perfetto::protos::pbzero::BuiltinClock;
using perfetto::protos::pbzero::TracePacket;

::perfetto::protos::pbzero::V8CodeLoad::Kind ToProto(CodeKind kind) {
  int32_t kind_value = static_cast<uint8_t>(kind) + 1;

  if (kind_value <= perfetto::protos::pbzero::V8CodeLoad_Kind_MAX) {
    return static_cast<perfetto::protos::pbzero::V8CodeLoad::Kind>(kind_value);
  }
  return ::perfetto::protos::pbzero::V8CodeLoad::KIND_UNKNOWN;
}

void WriteToProto(Tagged<Code> code,
                  ::perfetto::protos::pbzero::V8CodeLoad& v8_code_load) {
  v8_code_load.set_kind(ToProto(code->kind()));
  v8_code_load.set_start(code->instruction_start());
  v8_code_load.set_size(code->instruction_size());
  v8_code_load.set_native_code(
      reinterpret_cast<const uint8_t*>(code->instruction_start()),
      code->instruction_size());
}

void LogCodeCreate(Isolate* isolate, Handle<Code> code,
                   Handle<SharedFunctionInfo> function) {
  JitDataSource::Trace([&](JitDataSource::TraceContext ctx) {
    JitDataSource::TraceHandle handle(std::move(ctx), isolate);
    auto* code_load = handle.trace_packet()->set_v8_code_load();
    code_load->set_isolate_iid(handle.InternIsolate());
    code_load->set_function_iid(handle.InternFunction(*function));
    WriteToProto(*code, *code_load);
  });
}

class PerfettoJitLogger : public LogEventListener {
 public:
  explicit PerfettoJitLogger(Isolate* isolate) : isolate_(isolate) {}
  ~PerfettoJitLogger() override {}

  void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> abstract_code,
                       const char* name) override {
    DisallowGarbageCollection no_gc;
    if (!IsCode(*abstract_code, isolate_)) return;
    Tagged<Code> code = Code::cast(*abstract_code);
    JitDataSource::Trace([&](JitDataSource::TraceContext ctx) {
      JitDataSource::TraceHandle handle(std::move(ctx), isolate_);
      auto* code_load = handle.trace_packet()->set_v8_code_load();
      code_load->set_isolate_iid(handle.InternIsolate());
      code_load->set_function_iid(handle.InternNameOnlyFunction(name));
      WriteToProto(code, *code_load);
    });
  }

  void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> abstract_code,
                       Handle<Name> name) override {
    DisallowGarbageCollection no_gc;
    if (!IsCode(*abstract_code, isolate_)) return;
    Tagged<Code> code = Code::cast(*abstract_code);
    JitDataSource::Trace([&](JitDataSource::TraceContext ctx) {
      JitDataSource::TraceHandle handle(std::move(ctx), isolate_);
      auto* code_load = handle.trace_packet()->set_v8_code_load();
      code_load->set_isolate_iid(handle.InternIsolate());
      code_load->set_function_iid(handle.InternNameOnlyFunction(name));
      WriteToProto(code, *code_load);
    });
  }

  void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> abstract_code,
                       Handle<SharedFunctionInfo> shared,
                       Handle<Name> script_name) override {
    if (!IsCode(*abstract_code)) return;
    LogCodeCreate(isolate_, Handle<Code>::cast(abstract_code), shared);
  }

  void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> abstract_code,
                       Handle<SharedFunctionInfo> shared,
                       Handle<Name> script_name, int line,
                       int column) override {
    if (!IsCode(*abstract_code)) return;
    LogCodeCreate(isolate_, Handle<Code>::cast(abstract_code), shared);
  }

#if V8_ENABLE_WEBASSEMBLY
  void CodeCreateEvent(CodeTag tag, const wasm::WasmCode* code,
                       wasm::WasmName name, const char* source_url,
                       int code_offset, int script_id) override {}
#endif  // V8_ENABLE_WEBASSEMBLY

  void CallbackEvent(Handle<Name> name, Address entry_point) override {}
  void GetterCallbackEvent(Handle<Name> name, Address entry_point) override {}
  void SetterCallbackEvent(Handle<Name> name, Address entry_point) override {}
  void RegExpCodeCreateEvent(Handle<AbstractCode> code,
                             Handle<String> source) override {}
  void CodeMoveEvent(Tagged<InstructionStream> from,
                     Tagged<InstructionStream> to) override {}
  void BytecodeMoveEvent(Tagged<BytecodeArray> from,
                         Tagged<BytecodeArray> to) override {}
  void SharedFunctionInfoMoveEvent(Address from, Address to) override {}
  void NativeContextMoveEvent(Address from, Address to) override {}
  void CodeMovingGCEvent() override {}
  void CodeDisableOptEvent(Handle<AbstractCode> code,
                           Handle<SharedFunctionInfo> shared) override {}
  void CodeDeoptEvent(Handle<Code> code, DeoptimizeKind kind, Address pc,
                      int fp_to_sp_delta) override {}
  void CodeDependencyChangeEvent(Handle<Code> code,
                                 Handle<SharedFunctionInfo> sfi,
                                 const char* reason) override {}
  void WeakCodeClearEvent() override {}

  bool is_listening_to_code_events() override { return true; }

 private:
  Isolate* isolate_;
};

class IsolateRegistry {
 public:
  static IsolateRegistry& Get() {
    static IsolateRegistry* registry = new IsolateRegistry();
    return *registry;
  }

  void Add(Isolate* isolate) {
    base::LockGuard<base::Mutex> lock(&mutex_);
    auto insert = listeners_.emplace(isolate, nullptr);
    CHECK(insert.second);
    if (active_data_sources_ == 0) return;

    std::unique_ptr<LogEventListener>& listener = insert.first->second;
    listener = std::make_unique<PerfettoJitLogger>(isolate);
    isolate->logger()->AddListener(listener.get());
  }

  void Remove(Isolate* isolate) {
    base::LockGuard<base::Mutex> lock(&mutex_);
    auto it = listeners_.find(isolate);
    CHECK_NE(it, listeners_.end());

    std::unique_ptr<LogEventListener>& listener = it->second;

    if (listener) {
      isolate->logger()->RemoveListener(listener.get());
    }

    listeners_.erase(it);
  }

  void OnStartDataSource() {
    base::LockGuard<base::Mutex> lock(&mutex_);
    ++active_data_sources_;
    if (active_data_sources_ != 1) return;

    for (auto& entry : listeners_) {
      Isolate* isolate = entry.first;
      std::unique_ptr<LogEventListener>& listener = entry.second;
      CHECK_EQ(listener, nullptr);
      listener = std::make_unique<PerfettoJitLogger>(isolate);
      isolate->logger()->AddListener(listener.get());
    }
  }

  void OnStopDataSource() {
    base::LockGuard<base::Mutex> lock(&mutex_);
    --active_data_sources_;
    if (active_data_sources_ != 0) return;

    for (auto& entry : listeners_) {
      Isolate* isolate = entry.first;
      std::unique_ptr<LogEventListener>& listener = entry.second;
      CHECK_NE(listener, nullptr);
      isolate->logger()->RemoveListener(listener.get());
      listener.reset();
    }
  }

 private:
  IsolateRegistry() : active_data_sources_(0) {}

  base::Mutex mutex_;
  std::map<Isolate*, std::unique_ptr<LogEventListener>> listeners_;
  uint32_t active_data_sources_;
};

}  // namespace

// static
void JitDataSource::TraceRemapEmbeddedBuiltins(
    Isolate* isolate, const uint8_t* embedded_blob_code,
    size_t embedded_blob_code_size) {}

// static
void JitDataSource::TraceCodeRangeCreation(Isolate* isolate,
                                           const base::AddressRegion& region) {}

// static
void JitDataSource::TraceCodeRangeDestruction(
    Isolate* isolate, const base::AddressRegion& region) {}

// static
void JitDataSource::RegisterIsolate(Isolate* isolate) {
  IsolateRegistry::Get().Add(isolate);
}

// static
void JitDataSource::UnregisterIsolate(Isolate* isolate) {
  IsolateRegistry::Get().Remove(isolate);
}

void JitDataSource::OnSetup(const SetupArgs&) {}

void JitDataSource::OnStart(const StartArgs&) {
  IsolateRegistry::Get().OnStartDataSource();
}

void JitDataSource::OnStop(const StopArgs&) {
  IsolateRegistry::Get().OnStopDataSource();
}

JitDataSource::TraceHandle::TraceHandle(JitDataSource::TraceContext ctx,
                                        Isolate* isolate)
    : ctx_(std::move(ctx)),
      isolate_(*isolate),
      trace_packet_(ctx_.NewTracePacket()),
      incremental_state_(ctx_.GetIncrementalState()) {
  if (incremental_state_->was_cleared) {
    incremental_state_->was_cleared = false;
    trace_packet_->set_timestamp(perfetto::base::GetBootTimeNs().count());
    trace_packet_->set_sequence_flags(
        TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);
    trace_packet_->set_trace_packet_defaults()->set_timestamp_clock_id(
        BuiltinClock::BUILTIN_CLOCK_BOOTTIME);

    auto* thread = trace_packet_->set_thread_descriptor();
    thread->set_pid(base::OS::GetCurrentProcessId());
    thread->set_tid(base::OS::GetCurrentThreadId());

    // Need to call Finalize before we can call NewTracePacket
    trace_packet_->Finalize();
    trace_packet_ = ctx_.NewTracePacket();
  }

  trace_packet_->set_timestamp(perfetto::base::GetBootTimeNs().count());
  trace_packet_->set_sequence_flags(TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
}

JitDataSource::TraceHandle::~TraceHandle() {
  auto& serialized_interned_data = incremental_state_->serialized_interned_data;
  if (PERFETTO_LIKELY(serialized_interned_data.empty())) return;

  auto ranges = serialized_interned_data.GetRanges();
  trace_packet_->AppendScatteredBytes(
      perfetto::protos::pbzero::TracePacket::kInternedDataFieldNumber,
      &ranges[0], ranges.size());
  serialized_interned_data.Reset();
}

uint64_t JitDataSource::TraceHandle::InternFunction(
    Tagged<SharedFunctionInfo> function_info) {
  uint64_t function_name_iid =
      InternFunctionName(function_info->DebugNameCStr().get());

  if (!IsScript(function_info->script())) {
    return InternNameOnlyFunction(function_name_iid);
  }

  Function function;
  function.name_iid = function_name_iid;
  Tagged<Script> script = Script::cast(function_info->script());
  function.script_iid = InternScript(script);
  Script::PositionInfo info;
  Script::GetPositionInfo({script, &isolate_}, function_info->StartPosition(),
                          &info);
  function.line_num = info.line + 1;
  function.column_num = info.column + 1;

  if (auto it = incremental_state_->functions_.find(function);
      it != incremental_state_->functions_.end()) {
    return it->second;
  }

  auto iid = incremental_state_->next_function_iid++;
  auto* function_proto =
      incremental_state_->serialized_interned_data->add_v8_function();
  function_proto->set_iid(iid);
  function_proto->set_name_iid(function.name_iid);
  function_proto->set_script_iid(function.script_iid);
  function_proto->set_line_num(function.line_num);
  function_proto->set_column_num(function.column_num);

  incremental_state_->functions_.emplace(std::move(function), iid);

  return iid;
}

uint64_t JitDataSource::TraceHandle::InternNameOnlyFunction(uint64_t name_iid) {
  if (auto it = incremental_state_->name_only_functions_.find(name_iid);
      it != incremental_state_->name_only_functions_.end()) {
    return it->second;
  }

  auto iid = incremental_state_->next_function_iid++;
  auto* function_proto =
      incremental_state_->serialized_interned_data->add_v8_function();
  function_proto->set_iid(iid);
  function_proto->set_name_iid(name_iid);
  incremental_state_->name_only_functions_.emplace(name_iid, iid);

  return iid;
}

uint64_t JitDataSource::TraceHandle::InternNameOnlyFunction(const char* name) {
  if (!name) {
    return 0;
  }
  return InternNameOnlyFunction(InternFunctionName(std::string(name)));
}

uint64_t JitDataSource::TraceHandle::InternFunctionName(std::string name) {
  auto [it, inserted] = incremental_state_->function_names.emplace(
      std::move(name), incremental_state_->next_function_name_iid);
  uint64_t iid = it->second;
  const std::string& str = it->first;
  if (inserted) {
    ++incremental_state_->next_function_name_iid;
    auto* v8_function_name =
        incremental_state_->serialized_interned_data->add_v8_function_name();
    v8_function_name->set_iid(iid);
    v8_function_name->set_str(str);
  }
  return iid;
}

uint64_t JitDataSource::TraceHandle::InternNameOnlyFunction(Handle<Name> name) {
  if (Handle<String> str;
      Symbol::ToFunctionName(&isolate_, name).ToHandle(&str)) {
    return InternNameOnlyFunction(InternFunctionName(str->ToCString().get()));
  }
  return 0;
}

uint64_t JitDataSource::TraceHandle::InternScript(Tagged<Script> script) {
  if (!IsString(script->name())) {
    return 0;
  }

  std::string script_name(String::cast(script->name())->ToCString().get());

  auto it = incremental_state_->scripts_.insert(
      {script_name, incremental_state_->next_script_iid});
  bool inserted = it.second;
  uint64_t iid = it.first->second;
  if (inserted) {
    ++incremental_state_->next_script_iid;
    auto* script =
        incremental_state_->serialized_interned_data->add_v8_script();
    script->set_iid(iid);
    script->set_name(script_name);
  }
  return iid;
}

uint64_t JitDataSource::TraceHandle::InternIsolate() {
  auto [it, inserted] = incremental_state_->isolates_.insert(
      {isolate_.id(), incremental_state_->next_isolate_iid});
  uint64_t iid = it->second;
  if (inserted) {
    ++incremental_state_->next_isolate_iid;
    auto* isolate =
        incremental_state_->serialized_interned_data->add_v8_isolate();
    isolate->set_iid(iid);
    isolate->set_pid(base::OS::GetCurrentProcessId());
    isolate->set_isolate_id(isolate_.id());
    isolate->set_embedded_blob_code(
        reinterpret_cast<uint64_t>(isolate_.embedded_blob_code()));
    isolate->set_embedded_blob_code_size(isolate_.embedded_blob_code_size());
    if (auto* code_range = isolate_.heap()->code_range();
        code_range != nullptr) {
      auto* v8_code_range = isolate->set_code_range();
      v8_code_range->set_base(code_range->base());
      v8_code_range->set_size(code_range->size());
      if (auto* embedded_builtins_start = code_range->embedded_blob_code_copy();
          embedded_builtins_start != nullptr) {
        v8_code_range->set_embedded_blob_code_copy(
            reinterpret_cast<uint64_t>(embedded_builtins_start));
      }
    }
  }
  return iid;
}

}  // namespace internal
}  // namespace v8
