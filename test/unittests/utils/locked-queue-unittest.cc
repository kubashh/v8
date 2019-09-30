// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/time.h"
#include "src/utils/locked-queue-inl.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using Record = int;

}  // namespace

namespace v8 {
namespace internal {

TEST(LockedQueue, ConstructorEmpty) {
  LockedQueue<Record> queue;
  EXPECT_TRUE(queue.IsEmpty());
}

TEST(LockedQueue, SingleRecordEnqueueDequeue) {
  LockedQueue<Record> queue;
  EXPECT_TRUE(queue.IsEmpty());
  queue.Enqueue(1);
  EXPECT_FALSE(queue.IsEmpty());
  Record a = -1;
  bool success = queue.Dequeue(&a);
  EXPECT_TRUE(success);
  EXPECT_EQ(a, 1);
  EXPECT_TRUE(queue.IsEmpty());
}

TEST(LockedQueue, Peek) {
  LockedQueue<Record> queue;
  EXPECT_TRUE(queue.IsEmpty());
  queue.Enqueue(1);
  EXPECT_FALSE(queue.IsEmpty());
  Record a = -1;
  bool success = queue.Peek(&a);
  EXPECT_TRUE(success);
  EXPECT_EQ(a, 1);
  EXPECT_FALSE(queue.IsEmpty());
  success = queue.Dequeue(&a);
  EXPECT_TRUE(success);
  EXPECT_EQ(a, 1);
  EXPECT_TRUE(queue.IsEmpty());
}

TEST(LockedQueue, PeekOnEmpty) {
  LockedQueue<Record> queue;
  EXPECT_TRUE(queue.IsEmpty());
  Record a = -1;
  bool success = queue.Peek(&a);
  EXPECT_FALSE(success);
}

TEST(LockedQueue, MultipleRecords) {
  LockedQueue<Record> queue;
  EXPECT_TRUE(queue.IsEmpty());
  queue.Enqueue(1);
  EXPECT_FALSE(queue.IsEmpty());
  for (int i = 2; i <= 5; ++i) {
    queue.Enqueue(i);
    EXPECT_FALSE(queue.IsEmpty());
  }
  Record rec = 0;
  for (int i = 1; i <= 4; ++i) {
    EXPECT_FALSE(queue.IsEmpty());
    queue.Dequeue(&rec);
    EXPECT_EQ(i, rec);
  }
  for (int i = 6; i <= 12; ++i) {
    queue.Enqueue(i);
    EXPECT_FALSE(queue.IsEmpty());
  }
  for (int i = 5; i <= 12; ++i) {
    EXPECT_FALSE(queue.IsEmpty());
    queue.Dequeue(&rec);
    EXPECT_EQ(i, rec);
  }
  EXPECT_TRUE(queue.IsEmpty());
}

TEST(LockedQueue, MoveOnly) {
  using MoveOnlyRecord = std::unique_ptr<int>;
  LockedQueue<MoveOnlyRecord> queue;
  MoveOnlyRecord element = std::make_unique<int>(4);

  base::Optional<MoveOnlyRecord> empty = queue.Dequeue();
  ASSERT_FALSE(empty.has_value());

  queue.Enqueue(std::move(element));
  empty = queue.Dequeue();
  ASSERT_TRUE(empty.has_value());
  ASSERT_EQ(**empty, 4);

  empty = queue.Dequeue();
  ASSERT_FALSE(empty.has_value());

  empty = queue.DequeueWait(base::TimeDelta::FromMilliseconds(1));
  ASSERT_FALSE(empty.has_value());

  queue.Enqueue(std::make_unique<int>(4));
  empty = queue.DequeueWait(base::TimeDelta::FromMilliseconds(1));
  ASSERT_TRUE(empty.has_value());
  ASSERT_EQ(**empty, 4);
}

}  // namespace internal
}  // namespace v8
