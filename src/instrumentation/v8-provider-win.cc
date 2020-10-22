// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>

#include <type_traits>

#include "src/instrumentation/v8-provider.h"
// TODO(@sartang): This is a hack to get TraceLoggingProvider.h working. For
// some reason the API can't figure out what VOID is unless I define it first.
// It doesn't happen in a helloworld example so maybe there's something
// V8-specific causing this to happen? I'll keep investigating but in the
// meantime...
#ifdef VOID
#pragma push_macro("VOID")
#undef VOID
#define VOID void
#include <TraceLoggingProvider.h>
#pragma pop_macro("VOID")
#else
#define VOID void
#include <TraceLoggingProvider.h>
#undef VOID
#endif

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wc++98-compat-extra-semi"
#endif

namespace v8 {
namespace internal {
namespace tracing {

TRACELOGGING_DECLARE_PROVIDER(g_v8Provider);

// TODO(sartang@microsoft.com): when this is in msedge, change the GUID
TRACELOGGING_DEFINE_PROVIDER(g_v8Provider, "V8.js",
                             (0x57277741, 0x3638, 0x4A4B, 0xBD, 0xBA, 0x0A,
                              0xC6, 0xE4, 0x5D, 0xA5, 0x6C));

void Callback(const GUID* srcId, uint32_t providerState, uint8_t level,
              uint64_t matchAnyKeyword, uint64_t allKeyword, void* filter,
              void* context) {
  // TODO(sartang@microsoft.com): trace-config things
}

void V8Provider::RegisterProvider() {
  TraceLoggingRegisterEx(g_v8Provider,
                         reinterpret_cast<PENABLECALLBACK>(Callback), this);
}

void V8Provider::UnregisterProvider() {
  if (g_v8Provider) {
    TraceLoggingUnregister(g_v8Provider);
  }
}

bool V8Provider::IsEnabled() {
  return TraceLoggingProviderEnabled(g_v8Provider, 0, 0);
}

bool V8Provider::IsEnabled(const uint8_t level) {
  return TraceLoggingProviderEnabled(g_v8Provider, level, 0);
}

void V8Provider::AddTraceEvent(uint64_t id, const char* name, int num_args,
                               const char** arg_names, const uint8_t* arg_types,
                               const uint64_t* arg_values) {
  wchar_t* wName = new wchar_t[4096];
  MultiByteToWideChar(CP_ACP, 0, name, -1, wName, 4096);

  TraceLoggingWrite(g_v8Provider, "", TraceLoggingValue(wName, "Event Name"));
}

// Create the global "tracing::v8Provider" that is the instance of the provider
static_assert(std::is_trivial<V8Provider>::value, "V8Provider is not trivial");
V8Provider v8Provider{};

}  // namespace tracing
}  // namespace internal
}  // namespace v8
