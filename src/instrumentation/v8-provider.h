// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSTRUMENTATION_V8_PROVIDER_H_
#define V8_INSTRUMENTATION_V8_PROVIDER_H_

#include <stdint.h>

namespace v8 {
namespace internal {
namespace instrumentation {

class V8Provider {
 public:
  bool IsEnabled();
  bool IsEnabled(const uint8_t level);

  void RegisterProvider();
  void UnregisterProvider();

  void AddTraceEvent(uint64_t id, const char* name, int num_args,
                     const char** arg_names, const uint8_t* arg_types,
                     const uint64_t* arg_values);
};

// Declare the global "instrumentation::v8Provider" that is the instance of the
// provider
extern V8Provider v8Provider;

}  // namespace instrumentation
}  // namespace internal
}  // namespace v8

#endif  // V8_INSTRUMENTATION_V8_PROVIDER_H_
