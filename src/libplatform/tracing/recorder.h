// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBPLATFORM_TRACING_RECORDER_H_
#define V8_LIBPLATFORM_TRACING_RECORDER_H_

#include <stdint.h>

#include "include/libplatform/v8-tracing.h"

namespace v8 {
namespace platform {
namespace tracing {

class Recorder {
 public:
  Recorder();
  ~Recorder();

  bool IsEnabled();
  bool IsEnabled(const uint8_t level);

  void AddEvent(TraceObject* trace_event);
};

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_LIBPLATFORM_TRACING_RECORDER_H_
