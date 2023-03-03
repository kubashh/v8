// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_COMPILE_HINTS_H_
#define V8_PARSING_COMPILE_HINTS_H_

#include "include/v8-script.h"

#include <vector>

namespace v8 {
namespace internal {

class CompileHints {
public:
static v8::ScriptCompiler::CachedData* Serialize(std::vector<int>& compile_hints);
};

} }  // namespace v8::internal

#endif  // V8_PARSING_COMPILE_HINTS_H_