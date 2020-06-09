// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// For a good ETW overview, see
// https://docs.microsoft.com/en-us/archive/blogs/dcook/etw-overview

#ifndef V8_TRACING_PROVIDER_WIN_H_
#define V8_TRACING_PROVIDER_WIN_H_

#include "src/tracing/provider.h"
#include "include/v8config.h"

// Minimize dependencies. No platform specific inclusions (e.g. "Windows.h")
#include <cstdint>

namespace v8 {
namespace internal {
namespace tracing {

constexpr int kMaxTraitSize = 40;  // Provider name can be max 37 chars
struct ProviderState {
  uint64_t regHandle;
  uint32_t enabled;
  uint8_t level;
  uint64_t keywords;
  char provider_trait[kMaxTraitSize];
};

// The base class for providers
class WinProvider : public Provider {
 public:
  // Inline any calls to check the provider state
  uint8_t Level() { return state.level; }
  uint64_t Keywords() { return state.keywords; }

  bool IsEnabled() {
    if (V8_LIKELY(!state.enabled)) return false;
    return true;
  }

  bool IsEnabled(const EventInfo& event) {
    if (V8_LIKELY(!state.enabled)) return false;
    if (event.level > state.level) return false;
    return event.keywords == 0 || (event.keywords & state.keywords);
  }

  uint32_t Register(const GUID& guid, const char* providerName);
  void Unregister();

  // Derived classes need access to read the state for the logging calls
  const ProviderState& State() { return state; }

 private:
  static void Callback(const GUID* srcId, uint32_t providerState, uint8_t level,
                       uint64_t anyKeyword, uint64_t allKeyword, void* filter,
                       void* context);

  void UpdateState(bool isEnabled, uint8_t level, uint64_t keywords) {
    state.level = level;
    state.keywords = keywords;
    state.enabled = isEnabled;
  }
  uint64_t RegHandle() { return state.regHandle; }

  ProviderState state;
};  //  class WinProvider

}  // namespace tracing
}  // namespace internal
}  // namespace v8
#endif  // V8_TRACING_PROVIDER_WIN_H_
