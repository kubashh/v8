// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRACING_MAC_PROVIDER_H_
#define V8_TRACING_MAC_PROVIDER_H_

#include <os/signpost.h>

#include "include/v8.h"
#include "src/tracing/provider.h"

namespace v8 {
namespace internal {
namespace tracing {

class MacProvider : public Provider {
 public:
  // HACK
  uint8_t Level() { return 5; }

  bool IsEnabled() { return true; }
  bool IsEnabled(const EventInfo& event) { return true; }

  uint32_t Register(const GUID& guid, const char* providerName) {
    log = os_log_create(providerName, "");
    return 0;
  }

  void Unregister() {}

  ~MacProvider() {}

  os_log_t Log() { return log; }

 private:
  os_log_t log;
};  // class MacProvider

}  // namespace tracing
}  // namespace internal
}  // namespace v8

#endif  // V8_TRACING_PROVIDER_MAC_H_
