// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_INSTRUMENTATION_PROVIDER_WIN_H_
#define V8_INSTRUMENTATION_PROVIDER_WIN_H_

#include <windows.h>

#include "src/instrumentation/provider.h"
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
namespace instrumentation {

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

Provider::Provider() {
  TraceLoggingRegisterEx(g_v8Provider,
                         reinterpret_cast<PENABLECALLBACK>(Callback), this);
}

void Provider::RegisterProvider() {}

void Provider::UnregisterProvider() {
  if (g_v8Provider) {
    TraceLoggingUnregister(g_v8Provider);
  }
}

bool Provider::IsEnabled() {
  return TraceLoggingProviderEnabled(g_v8Provider, 0, 0);
}

bool Provider::IsEnabled(const uint8_t level) {
  return TraceLoggingProviderEnabled(g_v8Provider, level, 0);
}

void Provider::AddMainThreadEvent(const v8::metrics::Compile& event,
                                  v8::metrics::Recorder::ContextId context_id) {
  // if (v8::metrics::Recorder::GetContext(isolate, context_id).IsEmpty())
  // return;
  TraceLoggingWrite(g_v8Provider, "Compile");
}

}  // namespace instrumentation
}  // namespace internal
}  // namespace v8

#endif  // V8_INSTRUMENTATION_PROVIDER_WIN_H_
