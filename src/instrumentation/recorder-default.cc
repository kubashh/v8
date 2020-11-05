// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_INSTRUMENTATION_RECORDER_DEFAULT_H_
#define V8_INSTRUMENTATION_RECORDER_DEFAULT_H_

#include "src/instrumentation/recorder.h"

namespace v8 {
namespace internal {
namespace instrumentation {

Recorder::Recorder() {}
Recorder::~Recorder() {}

bool Recorder::IsEnabled() { return false; }
bool Recorder::IsEnabled(const uint8_t level) { return false; }

void Recorder::AddMainThreadEvent(const v8::metrics::Compile& event,
                                  v8::metrics::Recorder::ContextId id) {}

}  // namespace instrumentation
}  // namespace internal
}  // namespace v8

#endif  // V8_INSTRUMENTATION_RECORDER_DEFAULT_H_
