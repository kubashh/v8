// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRACING_V8_PROVIDER_H_
#define V8_TRACING_V8_PROVIDER_H_

/*
Provide name and GUID generated from it are:

    "V8.js",
    // {ca4c76aa-e822-589e-8f5d-9fdca8bad813}
    {0xca4c76aa,0xe822,0x589e,{0x8f,0x5d,0x9f,0xdc,0xa8,0xba,0xd8,0x13}};

Note: Below should be run from an admin prompt.

For simple testing, use "logman" to create a trace for this provider via:

  logman create trace -n v8js -o v8js.etl -p
{ca4c76aa-e822-589e-8f5d-9fdca8bad813}

After the provider GUID, you can optionally specificy keywords and level, e.g.

  -p {ca4c76aa-e822-589e-8f5d-9fdca8bad813} 0xBEEF 0x05

To capture events, start/stop the trace via:
  logman start example
  logman stop example

When finished recording, remove the configured trace via:

  logman delete example

Alternatively, use a tool such as PerfView or WPR to configure and record
traces.
*/

#if defined(V8_TARGET_OS_WIN)
#include "src/libplatform/tracing/provider-win.h"
#else
#include "src/libplatform/tracing/provider.h"
#endif  // V8_OS_WIN

#include "include/v8.h"

namespace v8 {
namespace platform {
namespace tracing {

class V8Provider {
#if defined(V8_TARGET_OS_WIN)

 public:
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
  bool IsEnabled(const uint8_t level);

  void RegisterProvider() {}
  void UnregisterProvider() {}

  void AddTraceEvent(uint64_t id, const char* name, int num_args,
                     const char** arg_names, const uint8_t* arg_types,
                     const uint64_t* arg_values) {}
  void CodeEventHandler(const JitCodeEvent* event) {}
#endif  // // V8_OS_WIN
};

// Declare the global "tracing::v8Provider" that is the instance of the provider
extern V8Provider v8Provider;

}  // namespace tracing
}  // namespace internal
}  // namespace v8

#endif  // V8_TRACING_V8_PROVIDER_H_
