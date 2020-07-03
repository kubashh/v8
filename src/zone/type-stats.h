// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ZONE_TYPE_STATS_H_
#define V8_ZONE_TYPE_STATS_H_

#include <typeindex>
#include <unordered_map>

#include "src/common/globals.h"

namespace v8 {
namespace internal {

class TypeStats;

#ifdef V8_ENABLE_PRECISE_ZONE_STATS

class TypeStats {
 public:
  TypeStats() = default;

  // Adds given delta to type_id's type.
  void Add(std::type_index type_id, size_t delta) { map_[type_id] += delta; }

  // Merges other stats into this stats object.
  void MergeWith(const TypeStats& other);

  // Prints recorded statisticts to stdout.
  void Dump() const;

 private:
  using HashMap = std::unordered_map<std::type_index, size_t>;
  HashMap map_;
};

#endif  // V8_ENABLE_PRECISE_ZONE_STATS

}  // namespace internal
}  // namespace v8

#endif  // V8_ZONE_TYPE_STATS_H_
