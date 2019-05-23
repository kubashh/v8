// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TOOLS_DEBUG_HELPER_HEAP_CONSTANTS_H_
#define V8_TOOLS_DEBUG_HELPER_HEAP_CONSTANTS_H_

#include <cstdint>
#include <string>

#include "debug-helper.h"

namespace d = v8::debug_helper;

namespace v8 {
namespace debug_helper_internal {

std::string FindKnownObject(uintptr_t address, const d::Roots& roots);
uint16_t FindKnownMapInstanceType(uintptr_t address, const d::Roots& roots);

}  // namespace debug_helper_internal
}  // namespace v8

#endif
