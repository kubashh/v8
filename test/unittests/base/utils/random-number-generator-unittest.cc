// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <climits>

#include "src/base/utils/random-number-generator.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace base {

class RandomNumberGeneratorTest : public ::testing::TestWithParam<int> {};


static const int kMaxRuns = 12345;

static void CheckSample(std::vector<int64_t> sample, int64_t max, size_t size) {
  EXPECT_EQ(sample.size(), size);

  // Check if values are unique.
  std::sort(sample.begin(), sample.end());
  EXPECT_EQ(std::adjacent_find(sample.begin(), sample.end()), sample.end());

  for (auto x : sample) {
    EXPECT_GE(x, 0);
    EXPECT_LT(x, max);
  }
}

TEST_P(RandomNumberGeneratorTest, NextIntWithMaxValue) {
  RandomNumberGenerator rng(GetParam());
  for (int max = 1; max <= kMaxRuns; ++max) {
    int n = rng.NextInt(max);
    EXPECT_LE(0, n);
    EXPECT_LT(n, max);
  }
}


TEST_P(RandomNumberGeneratorTest, NextBooleanReturnsFalseOrTrue) {
  RandomNumberGenerator rng(GetParam());
  for (int k = 0; k < kMaxRuns; ++k) {
    bool b = rng.NextBool();
    EXPECT_TRUE(b == false || b == true);
  }
}


TEST_P(RandomNumberGeneratorTest, NextDoubleReturnsValueBetween0And1) {
  RandomNumberGenerator rng(GetParam());
  for (int k = 0; k < kMaxRuns; ++k) {
    double d = rng.NextDouble();
    EXPECT_LE(0.0, d);
    EXPECT_LT(d, 1.0);
  }
}

TEST_P(RandomNumberGeneratorTest, NextSampleMin) {
  size_t m = 10;
  RandomNumberGenerator rng(GetParam());

  for (int k = 0; k < kMaxRuns; ++k) {
    std::vector<int64_t> sample = rng.NextSample(m, 1);

    CheckSample(sample, m, 1);
  }
}

TEST_P(RandomNumberGeneratorTest, NextSampleMax) {
  size_t m = 10;
  RandomNumberGenerator rng(GetParam());

  for (int k = 0; k < kMaxRuns; ++k) {
    std::vector<int64_t> sample = rng.NextSample(m, m - 1);

    CheckSample(sample, m, m - 1);
  }
}

TEST_P(RandomNumberGeneratorTest, NextSampleHalf) {
  size_t n = 5;
  int64_t m = 10;
  RandomNumberGenerator rng(GetParam());

  for (int k = 0; k < kMaxRuns; ++k) {
    std::vector<int64_t> sample = rng.NextSample(m, n);

    CheckSample(sample, m, n);
  }
}

TEST_P(RandomNumberGeneratorTest, NextSampleMoreThanHalf) {
  size_t n = 90;
  int64_t m = 100;
  RandomNumberGenerator rng(GetParam());

  for (int k = 0; k < kMaxRuns; ++k) {
    std::vector<int64_t> sample = rng.NextSample(m, n);

    CheckSample(sample, m, n);
  }
}

TEST_P(RandomNumberGeneratorTest, NextSampleLessThanHalf) {
  size_t n = 10;
  int64_t m = 100;
  RandomNumberGenerator rng(GetParam());

  for (int k = 0; k < kMaxRuns; ++k) {
    std::vector<int64_t> sample = rng.NextSample(m, n);

    CheckSample(sample, m, n);
  }
}

INSTANTIATE_TEST_CASE_P(RandomSeeds, RandomNumberGeneratorTest,
                        ::testing::Values(INT_MIN, -1, 0, 1, 42, 100,
                                          1234567890, 987654321, INT_MAX));

}  // namespace base
}  // namespace v8
