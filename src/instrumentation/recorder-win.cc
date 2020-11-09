// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_INSTRUMENTATION_RECORDER_WIN_H_
#define V8_INSTRUMENTATION_RECORDER_WIN_H_

#include <windows.h>
#include <TraceLoggingProvider.h>

#include "src/instrumentation/recorder.h"

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

void Recorder::AddMainThreadEvent(const v8::metrics::Compile& event,
                                  v8::metrics::Recorder::ContextId context_id) {
  // if (v8::metrics::Recorder::GetContext(isolate, context_id).IsEmpty())
  // return;
  TraceLoggingWrite(
      g_v8Provider, "Compile",
      TraceLoggingValue(event.wall_clock_duration_in_us, "duration (us)"));
}

}  // namespace instrumentation
}  // namespace internal
}  // namespace v8

#endif  // V8_INSTRUMENTATION_RECORDER_WIN_H_
