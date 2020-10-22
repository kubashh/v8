// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/instrumentation/v8-provider.h"

namespace v8 {
namespace internal {
namespace tracing {

bool V8Provider::IsEnabled() { return false; }
bool V8Provider::IsEnabled(const uint8_t level) { return false; }

void V8Provider::RegisterProvider() {}
void V8Provider::UnregisterProvider() {}

void V8Provider::AddTraceEvent(uint64_t id, const char* name, int num_args,
                               const char** arg_names, const uint8_t* arg_types,
                               const uint64_t* arg_values) {}
}  // namespace tracing
}  // namespace internal
}  // namespace v8
