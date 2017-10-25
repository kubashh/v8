// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/random-utils.h"
#include "src/base/utils/random-number-generator.h"

#include "testing/gtest-support.h"

namespace v8 {
namespace base {

std::vector<int64_t> Sample(size_t n, size_t max) {
  RandomNumberGenerator* gen = new base::RandomNumberGenerator(123);
  std::vector<int64_t> result = base::RandomSample(gen, max, n);
  delete gen;
  return result;
}

TEST(RandomSample, LessThanHalfLimit) {
  size_t n = 10;
  std::vector<int64_t> sample = Sample(n, 100);

  EXPECT_EQ(sample.size(), n);
}

TEST(RandomSample, MoreThanHalfLimit) {
  size_t n = 90;
  std::vector<int64_t> sample = Sample(n, 100);

  EXPECT_EQ(sample.size(), n);
}

TEST(RandomSample, CheckOutput) {
  size_t n = 4;
  std::vector<int64_t> sample = Sample(n, 10);

  EXPECT_EQ(sample.size(), n);

  std::sort(sample.begin(), sample.end());
  EXPECT_EQ(std::adjacent_find(sample.begin(), sample.end()), sample.end());

  for (auto x : sample) {
    EXPECT_GE(x, 0u);
    EXPECT_LT(x, 10u);
  }
}

}  // namespace base
}  // namespace v8
