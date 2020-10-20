// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRACING_V8_PROVIDER_H_
#define V8_TRACING_V8_PROVIDER_H_

#if defined(V8_TARGET_OS_WIN)
#include "src/libplatform/tracing/provider-win.h"
#else
#include "src/libplatform/tracing/provider.h"
#endif  // V8_OS_WIN

#include <stdint.h>

namespace v8 {
namespace platform {
namespace tracing {

class V8Provider {
#if defined(V8_TARGET_OS_WIN)

 public:
  uint8_t Level();

  bool IsEnabled();
  bool IsEnabled(const uint8_t level);

  void RegisterProvider() {
    provider = new WinProvider();
    provider->Register();
  }

  void UnregisterProvider() {
    provider->Unregister();
    if (provider) delete provider;
  }

  void AddTraceEvent(uint64_t id, const char* name, int num_args,
                     const char** arg_names, const uint8_t* arg_types,
                     const uint64_t* arg_values);

 private:
  WinProvider* provider;
#else

 public:
  bool IsEnabled() { return false; }
  bool IsEnabled(const uint8_t level) { return false; }

  void RegisterProvider() {}
  void UnregisterProvider() {}

  void AddTraceEvent(uint64_t id, const char* name, int num_args,
                     const char** arg_names, const uint8_t* arg_types,
                     const uint64_t* arg_values) {}
#endif  // // V8_OS_WIN
};

// Declare the global "tracing::v8Provider" that is the instance of the provider
extern V8Provider v8Provider;

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_TRACING_V8_PROVIDER_H_
