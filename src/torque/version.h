// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_VERSION_H_
#define V8_TORQUE_VERSION_H_

#include <cstdint>

namespace v8 {
namespace internal {
namespace torque {

// The language server uses this constant to check whether
// the running version is up-to-date or needs to be recompiled.
// Increment by one everytime the language server or Torque
// Compiler changes.
constexpr uint32_t kTorqueVersion = 1;

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_VERSION_H_
