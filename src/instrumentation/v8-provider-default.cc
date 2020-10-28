// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_INSTRUMENTATION_V8_PROVIDER_DEFAULT_H_
#define V8_INSTRUMENTATION_V8_PROVIDER_DEFAULT_H_

#include "src/instrumentation/v8-provider.h"

namespace v8 {
namespace internal {
namespace instrumentation {

V8Provider::V8Provider() {}

bool V8Provider::IsEnabled() { return false; }
bool V8Provider::IsEnabled(const uint8_t level) { return false; }

void V8Provider::RegisterProvider() {}
void V8Provider::UnregisterProvider() {}

void V8Provider::AddMainThreadEvent(const v8::metrics::Compile& event,
                                    v8::metrics::Recorder::ContextId id) {}

}  // namespace instrumentation
}  // namespace internal
}  // namespace v8

#endif  // V8_INSTRUMENTATION_V8_PROVIDER_DEFAULT_H_
