// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSTRUMENTATION_V8_PROVIDER_H_
#define V8_INSTRUMENTATION_V8_PROVIDER_H_

#include <stdint.h>

#include "include/v8-metrics.h"

namespace v8 {
namespace internal {
namespace instrumentation {

class V8_EXPORT V8Provider : public v8::metrics::Recorder {
 public:
  V8Provider();

  bool IsEnabled();
  bool IsEnabled(const uint8_t level);

  void RegisterProvider();
  void UnregisterProvider();

  void AddMainThreadEvent(const v8::metrics::Compile& event,
                          v8::metrics::Recorder::ContextId context_id) override;
};

}  // namespace instrumentation
}  // namespace internal
}  // namespace v8

#endif  // V8_INSTRUMENTATION_V8_PROVIDER_H_
