// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRACING_PROVIDER_H_
#define V8_TRACING_PROVIDER_H_

#include "include/v8.h"

namespace v8 {
namespace internal {
namespace tracing {

// Redefine the structure here to avoid including Windows headers
struct GUID {
  uint32_t Data1;
  uint16_t Data2;
  uint16_t Data3;
  uint8_t Data4[8];
};

struct EventInfo {
  uint16_t id;
  uint8_t level;
  uint8_t opcode;
  uint16_t task;
  uint64_t keywords;
};

class Provider {
 public:
  virtual uint8_t Level();

  virtual bool IsEnabled();
  virtual bool IsEnabled(const EventInfo& event);

  virtual uint32_t Register(const GUID& guid, const char* providerName);
  virtual void Unregister();

  virtual ~Provider() {}

 private:
  const char* providerName;
};

}  // namespace tracing
}  // namespace internal
}  // namespace v8

#endif  // V8_TRACING_PROVIDER_H_
