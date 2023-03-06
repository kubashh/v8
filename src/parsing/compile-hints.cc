// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/compile-hints.h"

#include <algorithm>

#include "src/handles/handles.h"
#include "src/objects/script.h"
#include "src/objects/shared-function-info.h"

namespace v8::internal {

v8::ScriptCompiler::CachedData* CompileHints::Serialize(
    std::vector<int>& compile_hints) {
  std::sort(compile_hints.begin(), compile_hints.end());

  size_t hints_count = compile_hints.size();
  int* data = new int[hints_count];
  for (size_t j = 0; j < hints_count; ++j) {
    data[j] = compile_hints[j];
  }
  // TODO: Better encoding.

  ScriptCompiler::CachedData* result = new ScriptCompiler::CachedData(
      reinterpret_cast<uint8_t*>(data),
      static_cast<int>(hints_count) * sizeof(int),
      ScriptCompiler::CachedData::BufferOwned);
  return result;
}

}  // namespace v8::internal
