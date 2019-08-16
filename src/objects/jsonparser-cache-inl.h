// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JSONPARSER_CACHE_INL_H_
#define V8_OBJECTS_JSONPARSER_CACHE_INL_H_

#include "src/objects/jsonparser-cache.h"

#include "src/objects/name-inl.h"
#include "src/objects/script-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/smi.h"
#include "src/objects/string.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

JsonParserCacheTable::JsonParserCacheTable(Address ptr)
    : HashTable<JsonParserCacheTable, JsonParserCacheShape>(ptr) {
  // SLOW_DCHECK(IsCompilationCacheTable());
}

NEVER_READ_ONLY_SPACE_IMPL(JsonParserCacheTable)
CAST_ACCESSOR(JsonParserCacheTable)

uint32_t JsonParserCacheShape::StringObjectHash(String source) {
  uint32_t hash = source.Hash();
  return hash;
}

uint32_t JsonParserCacheShape::HashForObject(ReadOnlyRoots roots,
                                             Object object) {
  if (object.IsNumber()) return static_cast<uint32_t>(object.Number());

  FixedArray val = FixedArray::cast(object);
  // DCHECK_EQ(4, val.length());
  String src = String::cast(val.get(0));
  // Object obj = Object::cast(val.get(1));
  return StringObjectHash(src);
}

// InfoCellPair::InfoCellPair(SharedFunctionInfo shared,
//                            FeedbackCell feedback_cell)
//     : is_compiled_scope_(!shared.is_null() ? shared.is_compiled_scope()
//                                            : IsCompiledScope()),
//       shared_(shared),
//       feedback_cell_(feedback_cell) {}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JSONPARSER_CACHE_INL_H_
