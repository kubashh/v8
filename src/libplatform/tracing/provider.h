// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRACING_PROVIDER_H_
#define V8_TRACING_PROVIDER_H_

namespace v8 {
namespace platform {
namespace tracing {

class Provider {
 public:
  virtual bool IsEnabled();
  virtual bool IsEnabled(uint8_t level);

  virtual void AddTraceEvent(uint64_t id, const char* name, int num_args,
                             const char** arg_names, const uint8_t* arg_types,
                             const uint64_t* arg_values);

  virtual uint32_t Register();
  virtual void Unregister();

  virtual ~Provider() {}
};

}  // namespace tracing
}  // namespace platform
}  // namespace v8

#endif  // V8_TRACING_PROVIDER_H_
