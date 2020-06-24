// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tracing/v8-provider.h"

#if defined(V8_TARGET_OS_WIN)
#include "src/tracing/etw-metadata.h"
#elif defined(V8_TARGET_OS_MACOSX)
#include <os/signpost.h>
#endif

#include <string>
#include <type_traits>

namespace v8 {
namespace internal {
namespace tracing {

#if defined(V8_TARGET_OS_WIN) || defined(V8_TARGET_OS_MACOSX)

uint8_t V8Provider::Level() { return provider->Level(); }

bool V8Provider::IsEnabled() { return provider->IsEnabled(); }
bool V8Provider::IsEnabled(const EventInfo& event) {
  return provider->IsEnabled(event);
}

#if defined(V8_TARGET_OS_WIN)

ProviderState V8Provider::State() { return provider->State(); }

void V8Provider::LogMsg(const char* msg) {
  constexpr static auto event_desc = etw::EventDescriptor(kMsgEvent);
  constexpr static auto event_meta =
      etw::EventMetadata("Msg", etw::Field("Msg", kTypeAnsiStr));

  LogEventData(State(), &event_desc, &event_meta, msg);
}

void V8Provider::LogInitializePlatform() {
  constexpr static auto event_desc =
      etw::EventDescriptor(kInitializePlatformEvent);
  constexpr static auto event_meta = etw::EventMetadata("InitializePlatform");

  LogEventData(State(), &event_desc, &event_meta);
}

void V8Provider::LogShutdownPlatform() {
  constexpr static auto event_desc =
      etw::EventDescriptor(kShutdownPlatformEvent);
  constexpr static auto event_meta = etw::EventMetadata("ShutdownPlatform");

  LogEventData(State(), &event_desc, &event_meta);
}

void V8Provider::LogInitializeV8() {
  constexpr static auto event_desc = etw::EventDescriptor(kInitializeV8Event);
  constexpr static auto event_meta = etw::EventMetadata("InitializeV8");

  LogEventData(State(), &event_desc, &event_meta);
}

void V8Provider::LogTearDownV8() {
  constexpr static auto event_desc = etw::EventDescriptor(kTearDownV8Event);
  constexpr static auto event_meta = etw::EventMetadata("TearDownV8");

  LogEventData(State(), &event_desc, &event_meta);
}

void V8Provider::LogIsolateStart(void* isolate) {
  constexpr static auto event_desc = etw::EventDescriptor(kIsolateStartEvent);
  constexpr static auto event_meta =
      etw::EventMetadata("IsolateStart", etw::Field("isolate", kTypePointer));

  LogEventData(State(), &event_desc, &event_meta, isolate);
}

void V8Provider::LogIsolateStop(void* isolate) {
  constexpr static auto event_desc = etw::EventDescriptor(kIsolateStopEvent);
  constexpr static auto event_meta =
      etw::EventMetadata("IsolateStop", etw::Field("isolate", kTypePointer));

  LogEventData(State(), &event_desc, &event_meta, isolate);
}

void V8Provider::LogSnapshotInitStart(void* isolate) {
  constexpr static auto event_desc =
      etw::EventDescriptor(kSnapshotInitStartEvent);
  constexpr static auto event_meta = etw::EventMetadata(
      "SnapshotInitStart", etw::Field("isolate", kTypePointer));

  LogEventData(State(), &event_desc, &event_meta, isolate);
}

void V8Provider::LogSnapshotInitStop(void* isolate) {
  constexpr static auto event_desc =
      etw::EventDescriptor(kSnapshotInitStopEvent);
  constexpr static auto event_meta = etw::EventMetadata(
      "SnapshotInitStop", etw::Field("isolate", kTypePointer));

  LogEventData(State(), &event_desc, &event_meta, isolate);
}

void V8Provider::LogParsingStart(void* isolate) {
  constexpr static auto event_desc = etw::EventDescriptor(kParsingStartEvent);
  constexpr static auto event_meta =
      etw::EventMetadata("ParsingStart", etw::Field("isolate", kTypePointer));

  LogEventData(State(), &event_desc, &event_meta, isolate);
}

void V8Provider::LogParsingStop(void* isolate) {
  constexpr static auto event_desc = etw::EventDescriptor(kParsingStopEvent);
  constexpr static auto event_meta =
      etw::EventMetadata("ParsingStop", etw::Field("isolate", kTypePointer));

  LogEventData(State(), &event_desc, &event_meta, isolate);
}

void V8Provider::LogGenerateUnoptimizedCodeStart(void* isolate) {
  constexpr static auto event_desc =
      etw::EventDescriptor(kGenerateUnoptimizedCodeStartEvent);
  constexpr static auto event_meta = etw::EventMetadata(
      "GenerateUnoptimizedCodeStart", etw::Field("isolate", kTypePointer));

  LogEventData(State(), &event_desc, &event_meta, isolate);
}

void V8Provider::LogGenerateUnoptimizedCodeStop(void* isolate) {
  constexpr static auto event_desc =
      etw::EventDescriptor(kGenerateUnoptimizedCodeStopEvent);
  constexpr static auto event_meta = etw::EventMetadata(
      "GenerateUnoptimizedCodeStop", etw::Field("isolate", kTypePointer));

  LogEventData(State(), &event_desc, &event_meta, isolate);
}

void V8Provider::LogJitExecuteStart() {
  constexpr static auto event_desc =
      etw::EventDescriptor(kJitExecuteStartEvent);
  constexpr static auto event_meta = etw::EventMetadata("JitExecuteStart");

  LogEventData(State(), &event_desc, &event_meta);
}

void V8Provider::LogJitExecuteStop() {
  constexpr static auto event_desc = etw::EventDescriptor(kJitExecuteStopEvent);
  constexpr static auto event_meta = etw::EventMetadata("JitExecuteStop");

  LogEventData(State(), &event_desc, &event_meta);
}

void V8Provider::LogJitFinalizeStart() {
  constexpr static auto event_desc =
      etw::EventDescriptor(kJitFinalizeStartEvent);
  constexpr static auto event_meta = etw::EventMetadata("JitFinalizeStart");

  LogEventData(State(), &event_desc, &event_meta);
}

void V8Provider::LogJitFinalizeStop() {
  constexpr static auto event_desc =
      etw::EventDescriptor(kJitFinalizeStopEvent);
  constexpr static auto event_meta = etw::EventMetadata("JitFinalizeStop");

  LogEventData(State(), &event_desc, &event_meta);
}

void V8Provider::LogDeopt(const std::string& reason, const std::string& kind,
                          const std::string& src, const std::string& fn,
                          int line, int column) {
  constexpr static auto event_desc = etw::EventDescriptor(kDeoptEvent);
  constexpr static auto event_meta = etw::EventMetadata(
      "Deopt", etw::Field("reason", kTypeAnsiStr),
      etw::Field("kind", kTypeAnsiStr), etw::Field("src", kTypeAnsiStr),
      etw::Field("fn", kTypeAnsiStr), etw::Field("line", kTypeInt32),
      etw::Field("column", kTypeInt32));

  LogEventData(State(), &event_desc, &event_meta, reason, kind, src, fn, line,
               column);
}

void V8Provider::LogDisableOpt(const std::string& fn_name,
                               const std::string& reason) {
  constexpr static auto event_desc = etw::EventDescriptor(kDisableOptEvent);
  constexpr static auto event_meta = etw::EventMetadata("DisableOpt");

  LogEventData(State(), &event_desc, &event_meta,
               etw::Field("fn", kTypeAnsiStr),
               etw::Field("reason", kTypeAnsiStr));
}

// Handle code event notifications and log Chakra-like ETW events.
void V8Provider::CodeEventHandler(const JitCodeEvent* event) {
  if (!v8Provider.IsEnabled() || v8Provider.Level() < kLevelInfo) return;
  v8Provider.LogCodeEvent(event);
}

void V8Provider::LogCodeEvent(const JitCodeEvent* event) {
  if (event->code_type != v8::JitCodeEvent::CodeType::JIT_CODE) return;

  // TODO(sartang@microsoft.com): Support/test interpreted code, RegExp, Wasm,
  // etc.
  constexpr static auto source_load_event_desc =
      etw::EventDescriptor(kSourceLoadEvent);
  constexpr static auto source_load_event_meta =
      etw::EventMetadata("SourceLoad", etw::Field("SourceID", kTypeUInt64),
                         etw::Field("ScriptContextID", kTypePointer),
                         etw::Field("SourceFlags", kTypeUInt32),
                         etw::Field("Url", kTypeUnicodeStr));

  constexpr static auto method_load_event_desc =
      etw::EventDescriptor(kMethodLoadEvent);
  constexpr static auto method_load_event_meta = etw::EventMetadata(
      "MethodLoad", etw::Field("ScriptContextID", kTypePointer),
      etw::Field("MethodStartAddress", kTypePointer),
      etw::Field("MethodSize", kTypeUInt64),
      etw::Field("MethodID", kTypeUInt32),
      etw::Field("MethodFlags", kTypeUInt16),
      etw::Field("MethodAddressRangeID", kTypeUInt16),
      etw::Field("SourceID", kTypeUInt64), etw::Field("Line", kTypeUInt32),
      etw::Field("Column", kTypeUInt32),
      etw::Field("MethodName", kTypeUnicodeStr));

  // TODO(sartang@microsoft.com): There are events for CODE_ADD_LINE_POS_INFO
  // and CODE_MOVED. Need these? Note: There is no event (currently) for code
  // being removed.
  if (event->type == v8::JitCodeEvent::EventType::CODE_ADDED) {
    int name_len = static_cast<int>(event->name.len);
    // Note: event->name.str is not null terminated.
    std::wstring method_name(name_len + 1, '\0');
    MultiByteToWideChar(
        CP_UTF8, 0, event->name.str, name_len,
        // Const cast needed as building with C++14 (not const in >= C++17)
        const_cast<LPWSTR>(method_name.data()),
        static_cast<int>(method_name.size()));

    void* script_context = static_cast<void*>(event->isolate);
    int script_id = 0;
    if (!event->script.IsEmpty()) {
      // if the first time seeing this source file, log the SourceLoad event
      script_id = event->script->GetId();
      auto& script_map = (*isolate_script_map)[script_context];
      if (script_map.find(script_id) == script_map.end()) {
        auto script_name = event->script->GetScriptName();
        if (script_name->IsString()) {
          auto v8str_name = script_name.As<v8::String>();
          std::wstring wstr_name(v8str_name->Length(), L'\0');
          // On Windows wchar_t == uint16_t. const_cast needed for C++14.
          uint16_t* wstr_data = const_cast<uint16_t*>(
              reinterpret_cast<const uint16_t*>(wstr_name.data()));
          v8str_name->Write(event->isolate, wstr_data);
          script_map.emplace(script_id, std::move(wstr_name));
        } else {
          script_map.emplace(script_id, std::wstring{L"[unknown]"});
        }
        const std::wstring& url = script_map[script_id];
        LogEventData(State(), &source_load_event_desc, &source_load_event_meta,
                     (uint64_t)script_id, (void*)script_context,
                     (uint32_t)0,  // SourceFlags
                     url);
      }
    }

    // TODO(sartang): Can there be more than one context per isolate to handle?
    LogEventData(State(), &method_load_event_desc, &method_load_event_meta,
                 (void*)script_context, (void*)event->code_start,
                 (uint64_t)event->code_len,
                 (uint32_t)0,  // MethodId
                 (uint16_t)0,  // MethodFlags
                 (uint16_t)0,  // MethodAddressRangeId
                 (uint64_t)script_id, (uint32_t)0,
                 (uint32_t)0,  // Line & Column
                 method_name);
  }
}

#elif defined(V8_TARGET_OS_MACOSX)
os_log_t V8Provider::Log() { return provider->Log(); }

void V8Provider::LogMsg(const char* msg) {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "", "%s", msg);
}

void V8Provider::LogInitializePlatform() {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "InitializePlatform");
}

void V8Provider::LogShutdownPlatform() {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "ShutdownPlatform");
}

void V8Provider::LogInitializeV8() {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "InitializeV8");
}

void V8Provider::LogTearDownV8() {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "TearDownV8");
}

void V8Provider::LogIsolateStart(void* isolate) {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "IsolateStart");
}

void V8Provider::LogIsolateStop(void* isolate) {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "IsolateStop");
}

void V8Provider::LogSnapshotInitStart(void* isolate) {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "SnapshotInitStart");
}

void V8Provider::LogSnapshotInitStop(void* isolate) {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "SnapshotInitStop");
}

void V8Provider::LogParsingStart(void* isolate) {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "ParsingStart");
}

void V8Provider::LogParsingStop(void* isolate) {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "ParsingStop");
}

void V8Provider::LogGenerateUnoptimizedCodeStart(void* isolate) {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE,
                         "GenerateUnoptimizedCodeStart");
}

void V8Provider::LogGenerateUnoptimizedCodeStop(void* isolate) {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE,
                         "GenerateUnoptimizedCodeStop");
}

void V8Provider::LogJitExecuteStart() {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "JitExecuteStart");
}

void V8Provider::LogJitExecuteStop() {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "JitExecuteStop");
}

void V8Provider::LogJitFinalizeStart() {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "JitFinalizeStart");
}

void V8Provider::LogJitFinalizeStop() {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "JitFinalizeStop");
}

void V8Provider::LogDeopt(const std::string& reason, const std::string& kind,
                          const std::string& src, const std::string& fn,
                          int line, int column) {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "Deopt",
                         "%s:%s:%s:%s:%d:%d", reason.c_str(), kind.c_str(),
                         src.c_str(), fn.c_str(), line, column);
}

void V8Provider::LogDisableOpt(const std::string& fn_name,
                               const std::string& reason) {
  os_signpost_event_emit(Log(), OS_SIGNPOST_ID_EXCLUSIVE, "DisableOpt", "%s:%s",
                         fn_name.c_str(), reason.c_str());
}

#endif  // defined(V8_TARGET_OS_MACOSX)

// Create the global "etw::v8Provider" that is the instance of the provider
static_assert(std::is_trivial<V8Provider>::value, "V8Provider is not trivial");
V8Provider v8Provider{};

#endif  // defined(V8_TARGET_OS_WIN) || defined(V8_TARGET_OS_MACOSX)
}  // namespace tracing
}  // namespace internal
}  // namespace v8
