// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// For a good ETW overview, see
// https://docs.microsoft.com/en-us/archive/blogs/dcook/etw-overview

#ifndef V8_TRACING_PROVIDER_WIN_H_
#define V8_TRACING_PROVIDER_WIN_H_

#include <windows.h>

#include <cstdint>

#include "include/v8config.h"
#include "src/libplatform/tracing/provider.h"
#ifndef VOID
#define VOID void
#endif
#include <TraceLoggingProvider.h>

namespace v8 {
namespace platform {
namespace tracing {

TRACELOGGING_DECLARE_PROVIDER(g_v8Provider);

struct ProviderState {
  TraceLoggingHProvider traceProvider;
};

// The base class for providers
class WinProvider : public Provider {
 public:
  bool IsEnabled() {
    if (V8_LIKELY(!TraceLoggingProviderEnabled(state.traceProvider, 0, 0)))
      return false;
    return true;
  }

  bool IsEnabled(const uint8_t level) {
    return TraceLoggingProviderEnabled(state.traceProvider, level, 0);
  }

  void AddTraceEvent(uint64_t id, const char* name, int num_args,
                     const char** arg_names, const uint8_t* arg_types,
                     const uint64_t* arg_values);

  uint32_t Register();
  void Unregister();

 private:
  static void Callback(const GUID* srcId, uint32_t providerState, uint8_t level,
                       uint64_t anyKeyword, uint64_t allKeyword, void* filter,
                       void* context);

  ProviderState state;
};  //  class WinProvider

}  // namespace tracing
}  // namespace internal
}  // namespace v8
#endif  // V8_TRACING_PROVIDER_WIN_H_
