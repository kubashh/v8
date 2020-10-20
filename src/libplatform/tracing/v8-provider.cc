// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/tracing/v8-provider.h"

#include <type_traits>

namespace v8 {
namespace platform {
namespace tracing {

#if defined(V8_TARGET_OS_WIN)

bool V8Provider::IsEnabled() { return provider->IsEnabled(); }
bool V8Provider::IsEnabled(const uint8_t level) {
  return provider->IsEnabled(level);
}
void V8Provider::AddTraceEvent(uint64_t id, const char* name, int num_args,
                               const char** arg_names, const uint8_t* arg_types,
                               const uint64_t* arg_values) {
  provider->AddTraceEvent(id, name, num_args, arg_names, arg_types, arg_values);
}

// Create the global "etw::v8Provider" that is the instance of the provider
static_assert(std::is_trivial<V8Provider>::value, "V8Provider is not trivial");
V8Provider v8Provider{};

#endif  // defined(V8_TARGET_OS_WIN)
}  // namespace tracing
}  // namespace platform
}  // namespace v8
