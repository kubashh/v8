// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <string>
#include <unordered_map>

#include "span.h"
#include "test_platform.h"

namespace v8_crdtp {
// =============================================================================
// span - sequence of bytes
// =============================================================================
template <typename T>
class SpanTest : public ::testing::Test {};

using TestTypes = ::testing::Types<uint8_t, uint16_t>;
TYPED_TEST_SUITE(SpanTest, TestTypes);

TYPED_TEST(SpanTest, Empty) {
  span<TypeParam> empty;
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(0u, empty.size());
  EXPECT_EQ(0u, empty.size_bytes());
  EXPECT_EQ(empty.begin(), empty.end());
}

TYPED_TEST(SpanTest, SingleItem) {
  TypeParam single_item = 42;
  span<TypeParam> singular(&single_item, 1);
  EXPECT_FALSE(singular.empty());
  EXPECT_EQ(1u, singular.size());
  EXPECT_EQ(sizeof(TypeParam), singular.size_bytes());
  EXPECT_EQ(singular.begin() + 1, singular.end());
  EXPECT_EQ(42, singular[0]);
}

TYPED_TEST(SpanTest, FiveItems) {
  std::vector<TypeParam> test_input = {31, 32, 33, 34, 35};
  span<TypeParam> five_items(test_input.data(), 5);
  EXPECT_FALSE(five_items.empty());
  EXPECT_EQ(5u, five_items.size());
  EXPECT_EQ(sizeof(TypeParam) * 5, five_items.size_bytes());
  EXPECT_EQ(five_items.begin() + 5, five_items.end());
  EXPECT_EQ(31, five_items[0]);
  EXPECT_EQ(32, five_items[1]);
  EXPECT_EQ(33, five_items[2]);
  EXPECT_EQ(34, five_items[3]);
  EXPECT_EQ(35, five_items[4]);
  span<TypeParam> three_items = five_items.subspan(2);
  EXPECT_EQ(3u, three_items.size());
  EXPECT_EQ(33, three_items[0]);
  EXPECT_EQ(34, three_items[1]);
  EXPECT_EQ(35, three_items[2]);
  span<TypeParam> two_items = five_items.subspan(2, 2);
  EXPECT_EQ(2u, two_items.size());
  EXPECT_EQ(33, two_items[0]);
  EXPECT_EQ(34, two_items[1]);
}

TEST(SpanFromTest, FromConstCharAndLiteral) {
  // Testing this is useful because strlen(nullptr) is undefined.
  EXPECT_EQ(nullptr, SpanFrom(nullptr).data());
  EXPECT_EQ(0u, SpanFrom(nullptr).size());

  const char* kEmpty = "";
  EXPECT_EQ(kEmpty, reinterpret_cast<const char*>(SpanFrom(kEmpty).data()));
  EXPECT_EQ(0u, SpanFrom(kEmpty).size());

  const char* kFoo = "foo";
  EXPECT_EQ(kFoo, reinterpret_cast<const char*>(SpanFrom(kFoo).data()));
  EXPECT_EQ(3u, SpanFrom(kFoo).size());

  EXPECT_EQ(3u, SpanFrom("foo").size());
}

TEST(SpanFromTest, FromVectorUint8AndUint16) {
  std::vector<uint8_t> foo = {'f', 'o', 'o'};
  span<uint8_t> foo_span = SpanFrom(foo);
  EXPECT_EQ(foo.size(), foo_span.size());

  std::vector<uint16_t> bar = {0xff, 0xef, 0xeb};
  span<uint16_t> bar_span = SpanFrom(bar);
  EXPECT_EQ(bar.size(), bar_span.size());
}

TEST(SpanComparisons, ByteWiseLexicographicalOrder) {
  // Compare the empty span.
  EXPECT_FALSE(SpanLessThan(span<uint8_t>(), span<uint8_t>()));
  EXPECT_TRUE(SpanEquals(span<uint8_t>(), span<uint8_t>()));

  // Compare message with itself.
  std::string msg = "Hello, world";
  EXPECT_FALSE(SpanLessThan(SpanFrom(msg), SpanFrom(msg)));
  EXPECT_TRUE(SpanEquals(SpanFrom(msg), SpanFrom(msg)));

  // Compare message and copy.
  EXPECT_FALSE(SpanLessThan(SpanFrom(msg), SpanFrom(std::string(msg))));
  EXPECT_TRUE(SpanEquals(SpanFrom(msg), SpanFrom(std::string(msg))));

  // Compare two messages. |lesser_msg| < |msg| because of the first
  // byte ('A' < 'H').
  std::string lesser_msg = "A lesser message.";
  EXPECT_TRUE(SpanLessThan(SpanFrom(lesser_msg), SpanFrom(msg)));
  EXPECT_FALSE(SpanLessThan(SpanFrom(msg), SpanFrom(lesser_msg)));
  EXPECT_FALSE(SpanEquals(SpanFrom(msg), SpanFrom(lesser_msg)));
}

// =============================================================================
// FindByFirst - Efficient retrieval from a sorted vector.
// =============================================================================
TEST(FindByFirst, SpanBySpan) {
  std::vector<std::pair<span<uint8_t>, span<uint8_t>>> sorted_span_by_span = {
      {SpanFrom("foo1"), SpanFrom("bar1")},
      {SpanFrom("foo2"), SpanFrom("bar2")},
      {SpanFrom("foo3"), SpanFrom("bar3")},
  };
  {
    auto result = FindByFirst(sorted_span_by_span, SpanFrom("foo1"),
                              SpanFrom("not_found"));
    EXPECT_EQ("bar1", std::string(result.begin(), result.end()));
  }
  {
    auto result = FindByFirst(sorted_span_by_span, SpanFrom("foo3"),
                              SpanFrom("not_found"));
    EXPECT_EQ("bar3", std::string(result.begin(), result.end()));
  }
  {
    auto result = FindByFirst(sorted_span_by_span, SpanFrom("baz"),
                              SpanFrom("not_found"));
    EXPECT_EQ("not_found", std::string(result.begin(), result.end()));
  }
}

namespace {
class TestObject {
 public:
  explicit TestObject(const std::string& message) : message_(message) {}

  const std::string& message() const { return message_; }

 private:
  std::string message_;
};
}  // namespace

TEST(FindByFirst, ObjectBySpan) {
  std::vector<std::pair<span<uint8_t>, std::unique_ptr<TestObject>>>
      sorted_object_by_span;
  sorted_object_by_span.push_back(
      std::make_pair(SpanFrom("foo1"), std::make_unique<TestObject>("bar1")));
  sorted_object_by_span.push_back(
      std::make_pair(SpanFrom("foo2"), std::make_unique<TestObject>("bar2")));
  sorted_object_by_span.push_back(
      std::make_pair(SpanFrom("foo3"), std::make_unique<TestObject>("bar3")));
  {
    TestObject* result =
        FindByFirst<TestObject>(sorted_object_by_span, SpanFrom("foo1"));
    ASSERT_TRUE(result);
    ASSERT_EQ("bar1", result->message());
  }
  {
    TestObject* result =
        FindByFirst<TestObject>(sorted_object_by_span, SpanFrom("foo3"));
    ASSERT_TRUE(result);
    ASSERT_EQ("bar3", result->message());
  }
  {
    TestObject* result =
        FindByFirst<TestObject>(sorted_object_by_span, SpanFrom("baz"));
    ASSERT_FALSE(result);
  }
}
}  // namespace v8_crdtp
