// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/nested-hash-map.h"

#include "src/base/functional.h"
#include "src/base/optional.h"
#include "src/base/platform/elapsed-timer.h"
#include "src/base/vector.h"
#include "test/unittests/test-utils.h"

namespace v8::internal::compiler::turboshaft {

class NestedHashMapTest : public TestWithPlatform {};

TEST_F(NestedHashMapTest, BasicTest) {
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  using Map = NestedHashMap<int, int>;
  for (size_t j = 0; j < 100; ++j) {
    Map map(&zone);
    std::map<int, int> ref_map;
    for (int i = 0; i < 1000; ++i) {
      int key = static_cast<int>(base::hash_combine(483012123 + j, i));
      map[key] = i + 1;
      ref_map[key] = i + 1;
      EXPECT_EQ(*map.Find(key), i + 1);
    }
    for (auto& [k, v] : ref_map) {
      EXPECT_EQ(*map.Find(k), v);
      EXPECT_EQ(map[k], v);
    }
  }
}

}  // namespace v8::internal::compiler::turboshaft
