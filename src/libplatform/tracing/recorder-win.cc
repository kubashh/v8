// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_LIBPLATFORM_TRACING_RECORDER_WIN_H_
#define V8_LIBPLATFORM_TRACING_RECORDER_WIN_H_

#include <windows.h>
#include <TraceLoggingProvider.h>

#include "include/libplatform/v8-tracing.h"

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wc++98-compat-extra-semi"
#endif

#ifndef V8_ETW_GUID
#define V8_ETW_GUID \
  0x57277741, 0x3638, 0x4A4B, 0xBD, 0xBA, 0x0A, 0xC6, 0xE4, 0x5D, 0xA5, 0x6C
#endif

namespace v8 {
namespace platform {
namespace tracing {

TRACELOGGING_DECLARE_PROVIDER(g_v8Provider);

TRACELOGGING_DEFINE_PROVIDER(g_v8Provider, "V8.js", (V8_ETW_GUID));

Recorder::Recorder() { TraceLoggingRegister(g_v8Provider); }

Recorder::~Recorder() {
  if (g_v8Provider) {
    TraceLoggingUnregister(g_v8Provider);
  }
}

bool Recorder::IsEnabled() {
  return TraceLoggingProviderEnabled(g_v8Provider, 0, 0);
}

bool Recorder::IsEnabled(const uint8_t level) {
  return TraceLoggingProviderEnabled(g_v8Provider, level, 0);
}

uint64_t Recorder::AddEvent(const char* name) {
  wchar_t* wName = new wchar_t[4096];
  MultiByteToWideChar(CP_ACP, 0, name, -1, wName, 4096);

  TraceLoggingWrite(g_v8Provider, "", TraceLoggingValue(wName, "Event Name"));
  return 0;
}

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_TRACING_RECORDER_WIN_H_
