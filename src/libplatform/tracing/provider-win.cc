// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/tracing/provider-win.h"

#include <windows.h>
#ifndef VOID
#define VOID void
#endif
#include <TraceLoggingProvider.h>

namespace v8 {
namespace platform {
namespace tracing {

// TODO(sartang@microsoft.com): when this is in msedge, change the GUID
TRACELOGGING_DEFINE_PROVIDER(g_v8Provider, "V8.js",
                             (0x57277741, 0x3638, 0x4A4B, 0xBD, 0xBA, 0x0A,
                              0xC6, 0xE4, 0x5D, 0xA5, 0x6C));

uint32_t WinProvider::Register() {
  TraceLoggingRegisterEx(g_v8Provider,
                         reinterpret_cast<PENABLECALLBACK>(Callback), this);
  state.traceProvider = g_v8Provider;
  return 0;
}

void WinProvider::Unregister() {
  if (g_v8Provider) {
    TraceLoggingUnregister(g_v8Provider);
  }
}

void WinProvider::Callback(const GUID* srcId, uint32_t providerState,
                           uint8_t level, uint64_t matchAnyKeyword,
                           uint64_t allKeyword, void* filter, void* context) {
  // TODO: trace-config things
}

void WinProvider::AddTraceEvent(uint64_t id, const char* name, int num_args,
                                const char** arg_names,
                                const uint8_t* arg_types,
                                const uint64_t* arg_values) {
  wchar_t* wName = new wchar_t[4096];
  MultiByteToWideChar(CP_ACP, 0, name, -1, wName, 4096);

  TraceLoggingWrite(state.traceProvider, "",
                    TraceLoggingValue(wName, "Event Name"));
}

}  // namespace tracing
}  // namespace internal
}  // namespace v8
