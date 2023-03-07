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
    std::vector<int>& compile_hints, int64_t prefix) {
  std::sort(compile_hints.begin(), compile_hints.end());

  size_t hints_count = compile_hints.size();
  constexpr size_t prefix_size = sizeof(int64_t);
  size_t data_size = hints_count * sizeof(int) + prefix_size;
  uint8_t* data = new byte[data_size];

  // Add the prefix in a little-endian manner.
  size_t ix = 0;
  for (size_t i = 0; i < prefix_size; ++i) {
    data[ix++] = prefix & 0xff;
    prefix >>= 8;
  }

  for (size_t j = 0; j < hints_count; ++j) {
    // Add every int in a little-endian manner.
    int hint = compile_hints[j];
    for (size_t k = 0; k < sizeof(int); ++k) {
      data[ix++] = hint & 0xff;
      hint >>= 8;
    }
  }
  DCHECK_EQ(data_size, ix);

  ScriptCompiler::CachedData* result =
      new ScriptCompiler::CachedData(data, static_cast<int>(data_size),
                                     ScriptCompiler::CachedData::BufferOwned);
  return result;
}

std::vector<int> CompileHints::Deserialize(const byte* data, int length) {
  // Discard the int64_t prefix.
  constexpr size_t prefix_size = sizeof(int64_t);
  size_t ix = prefix_size;
  std::vector<int> result;
  size_t compile_hint_count = (length - prefix_size) / sizeof(int);
  result.reserve(compile_hint_count);
  // Read every int in a little-endian manner.
  for (size_t i = 0; i < compile_hint_count; ++i) {
    int hint = 0;
    for (size_t j = 0; j < sizeof(int); ++j) {
      if (j > 0) {
        hint <<= 8;
      }
      hint |= data[ix++];
    }
    result.push_back(hint);
  }
  return result;
}

}  // namespace v8::internal
