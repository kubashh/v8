// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <tuple>

#include "src/compiler/persistent.h"

namespace v8 {
namespace internal {
namespace compiler {

TEST(PersMap, JustAdd) {
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  unsigned int seed = 19457;
  PersMap<int, int> map(&zone);

  for (int i = 0; i < 2000000; ++i) {
    int key = rand_r(&seed) % 2000000;
    int value = rand_r(&seed) % 20;
    map[key] = value;
  }
  int sum = 0;
  for (int j = 0; j < 5; j++) {
    sum = 0;
    for (auto pair : map) {
      sum += pair.second;
    }
  }
  ASSERT_LT(100, sum);
}

TEST(PersMap, CreateMany) {
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  unsigned int seed = 19457;
  for (int i = 0; i < 100000; ++i) {
    PersMap<int, int> map(&zone);
    for (int i = 0; i < 10; ++i) {
      int key = rand_r(&seed) % 2000000;
      int value = rand_r(&seed) % 20;
      map[key] = value;
    }
    int sum = 0;
    for (auto it = map.begin(); it.has_next(); ++it) {
      sum += (*it).second;
    }
    ASSERT_LT(0, sum);
  }
}

TEST(PersMap, CreateManyRef) {
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  unsigned int seed = 19457;
  for (int i = 0; i < 100000; ++i) {
    ZoneMap<int, int> map(&zone);
    for (int i = 0; i < 10; ++i) {
      int key = rand_r(&seed) % 2000000;
      int value = rand_r(&seed) % 20;
      map[key] = value;
    }
    int sum = 0;
    for (auto pair : map) {
      sum += pair.second;
    }
    ASSERT_LT(0, sum);
  }
}

TEST(PersMap, RefAdd) {
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  unsigned int seed = 19457;
  ZoneMap<int, int> map(&zone);

  for (int i = 0; i < 2000000; ++i) {
    int key = rand_r(&seed) % 2000000;
    int value = rand_r(&seed) % 20;
    map[key] = value;
  }
  int sum = 0;
  for (int j = 0; j < 5; j++) {
    sum = 0;
    for (auto pair : map) {
      sum += pair.second;
    }
  }
  ASSERT_LT(100, sum);
}

TEST(PersMap, AddAndQuery) {
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  unsigned int seed = 19457;

  struct bad_hash {
    size_t operator()(int key) { return static_cast<size_t>(key) % 200; }
  };
  PersMap<int, int, bad_hash> map(&zone);
  auto old_map = map;
  std::map<int, int> ref_map;
  auto old_ref_map = ref_map;

  for (int i = 0; i < 5000; ++i) {
    for (int j = 0; j < 100; j++) {
      ASSERT_EQ(ref_map[j], map[j]);
    }
    int key = rand_r(&seed);
    int value = rand_r(&seed);
    map[key] = value;
    ref_map[key] = value;
    if (rand_r(&seed) % 10 == 0) {
      map = old_map;
      ref_map = old_ref_map;
    }
    if (rand_r(&seed) % 10 == 0) {
      old_map = map;
      old_ref_map = ref_map;
    }
  }

  std::map<int, int> ref_map2;
  for (const auto& pair : map) {
    ref_map2[pair.first] = pair.second;
  }
  for (const auto& pair : ref_map) {
    int key, value;
    std::tie(key, value) = pair;
    if (value != 0) {
      ASSERT_EQ(ref_map[key], ref_map2[key]);
    }
  }
}

TEST(PersMap, Zip) {
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  unsigned int seed = 19457;

  struct bad_hash {
    size_t operator()(int key) { return static_cast<size_t>(key) % 100; }
  };
  PersMap<int, int, bad_hash> a(&zone);
  PersMap<int, int, bad_hash> b(&zone);

  int sum_a = 0;
  int sum_b = 0;

  for (int i = 0; i < 50000; ++i) {
    int key = rand_r(&seed) % 1000;
    int value = rand_r(&seed) % 100;
    if (true || key % 3 == 1) {
      sum_a += value;
      a[key] = a[key] + value;
    } else {
      sum_b += value;
      b[key] = b[key] + value;
    }
  }

  int sum = sum_a + sum_b;

  for (auto pair : a) {
    sum_a -= pair.second;
  }
  ASSERT_EQ(0, sum_a);

  for (auto pair : b) {
    sum_b -= pair.second;
  }
  ASSERT_EQ(0, sum_b);

  for (auto pair : PersMap<int, int, bad_hash>::Zip(
           a, b, [](int x, int y) { return x + y; })) {
    sum -= pair.second;
  }
  ASSERT_EQ(0, sum);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
