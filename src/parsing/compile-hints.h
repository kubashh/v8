// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_COMPILE_HINTS_H_
#define V8_PARSING_COMPILE_HINTS_H_

#include <vector>

#include "include/v8-script.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class AlignedCachedData;

class CompileHints {
 public:
  static v8::ScriptCompiler::CachedData* Serialize(
      std::vector<int>& compile_hints, int64_t prefix);
  static std::vector<int> Deserialize(AlignedCachedData* data);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_COMPILE_HINTS_H_
