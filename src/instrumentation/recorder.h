// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSTRUMENTATION_RECORDER_H_
#define V8_INSTRUMENTATION_RECORDER_H_

#include <stdint.h>

#include "include/v8-metrics.h"
#include "src/base/macros.h"

namespace v8 {
namespace internal {
namespace instrumentation {

class V8_EXPORT_PRIVATE Recorder : public v8::metrics::Recorder {
 public:
  Recorder();
  ~Recorder();

  bool IsEnabled();
  bool IsEnabled(const uint8_t level);

  void AddMainThreadEvent(const v8::metrics::Compile& event,
                          v8::metrics::Recorder::ContextId context_id) override;
};

}  // namespace instrumentation
}  // namespace internal
}  // namespace v8

#endif  // V8_INSTRUMENTATION_RECORDER_H_
