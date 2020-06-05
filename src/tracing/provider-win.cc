// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tracing/provider-win.h"

#include <evntprov.h>
#include <Windows.h>

namespace v8 {
namespace internal {
namespace tracing {

uint32_t Provider::Register(const GUID& guid, const char* providerName) {
  // Use a local static to ensure registration only happens once
  // Note: This means it cannot be "re-registered" after being unregistered.
  static uint32_t hr = EventRegister(
      reinterpret_cast<const ::GUID*>(&guid),
      reinterpret_cast<PENABLECALLBACK>(Callback), this, &state.regHandle);

  // Copy the provider name, prefixed by a UINT16 length, to the provider traits
  // The string in the buffer should be null terminated.
  // See https://docs.microsoft.com/en-us/windows/win32/etw/provider-traits
  uint16_t trait_size = sizeof(uint16_t) + strlen(providerName) + 1;
  *reinterpret_cast<uint16_t*>(&state.provider_trait[0]) = trait_size;
  strcpy_s(&state.provider_trait[2], kMaxTraitSize - 2, providerName);

  return hr;
}

void Provider::Unregister() {
  if (state.regHandle) {
    EventUnregister(state.regHandle);
    state.regHandle = 0;
    state.enabled = 0;
    state.level = 0;
    state.keywords = 0;
  }
}

void Provider::Callback(const GUID* srcId, uint32_t providerState,
                        uint8_t level, uint64_t matchAnyKeyword,
                        uint64_t allKeyword, void* filter, void* context) {
  WinProvider* provider = static_cast<WinProvider*>(context);
  switch (providerState) {
    case 0:  // Disabled
      provider->UpdateState(false, 0, 0);
      break;
    case 1:  // Enabled
      // level and keywords have all bits set if not specified by the session
      provider->UpdateState(true, level, matchAnyKeyword);
      break;
    default:
      // Ignore
      break;
  }
}

}  // namespace tracing
}  // namespace internal
}  // namespace v8
