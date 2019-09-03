// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_GLOBALS_H_
#define V8_COMPILER_GLOBALS_H_

// #include <stddef.h>
#include <stdint.h>

// #include <limits>
#include <ostream>

// #include "include/v8-internal.h"
// #include "src/base/atomic-utils.h"
// #include "src/base/build_config.h"
// #include "src/base/flags.h"
#include "src/base/logging.h"
#include "src/common/globals.h"
// #include "src/base/macros.h"

namespace v8 {
namespace internal {

enum class StackCheckKind {
  kFunctionEntry = 0,
  kIterationBody = 1,
  kUnknown = 2,
};

inline std::ostream& operator<<(std::ostream& os, StackCheckKind kind) {
  switch (kind) {
    case StackCheckKind::kFunctionEntry:
      return os << "FunctionEntry";
    case StackCheckKind::kIterationBody:
      return os << "IterationBody";
    case StackCheckKind::kUnknown:
      return os << "Unknown";
  }
  UNREACHABLE();
}

inline size_t hash_value(StackCheckKind kind) {
  return static_cast<size_t>(kind);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_GLOBALS_H_
