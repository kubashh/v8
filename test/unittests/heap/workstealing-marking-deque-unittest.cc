// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/workstealing-marking-deque.h"

#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

class HeapObject {};

TEST(WorkStealingBag, SegmentCreate) {
  WorkStealingBag::Segment segment;
  EXPECT_TRUE(segment.IsEmpty());
  EXPECT_EQ(0u, segment.Size());
  EXPECT_FALSE(segment.IsFull());
}

TEST(WorkStealingBag, SegmentPush) {
  WorkStealingBag::Segment segment;
  EXPECT_EQ(0u, segment.Size());
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_EQ(1u, segment.Size());
}

TEST(WorkStealingBag, SegmentPushPop) {
  WorkStealingBag::Segment segment;
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_EQ(1u, segment.Size());
  HeapObject dummy;
  HeapObject* object = &dummy;
  EXPECT_TRUE(segment.Pop(&object));
  EXPECT_EQ(0u, segment.Size());
  EXPECT_EQ(nullptr, object);
}

TEST(WorkStealingBag, SegmentIsEmpty) {
  WorkStealingBag::Segment segment;
  EXPECT_TRUE(segment.IsEmpty());
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_FALSE(segment.IsEmpty());
}

TEST(WorkStealingBag, SegmentIsFull) {
  WorkStealingBag::Segment segment;
  EXPECT_FALSE(segment.IsFull());
  for (size_t i = 0; i < WorkStealingBag::Segment::kCapacity; i++) {
    EXPECT_TRUE(segment.Push(nullptr));
  }
  EXPECT_TRUE(segment.IsFull());
}

TEST(WorkStealingBag, SegmentClear) {
  WorkStealingBag::Segment segment;
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_FALSE(segment.IsEmpty());
  segment.Clear();
  EXPECT_TRUE(segment.IsEmpty());
  for (size_t i = 0; i < WorkStealingBag::Segment::kCapacity; i++) {
    EXPECT_TRUE(segment.Push(nullptr));
  }
}

TEST(WorkStealingBag, SegmentFullPushFails) {
  WorkStealingBag::Segment segment;
  EXPECT_FALSE(segment.IsFull());
  for (size_t i = 0; i < WorkStealingBag::Segment::kCapacity; i++) {
    EXPECT_TRUE(segment.Push(nullptr));
  }
  EXPECT_TRUE(segment.IsFull());
  EXPECT_FALSE(segment.Push(nullptr));
}

TEST(WorkStealingBag, SegmentEmptyPopFails) {
  WorkStealingBag::Segment segment;
  EXPECT_TRUE(segment.IsEmpty());
  HeapObject* object;
  EXPECT_FALSE(segment.Pop(&object));
}

TEST(WorkStealingBag, LocalEmpty) {
  WorkStealingBag marking_bag;
  LocalWorkStealingBag local_marking_bag(&marking_bag, 0);
  EXPECT_TRUE(local_marking_bag.IsLocalEmpty());
}

TEST(WorkStealingBag, LocalPushPop) {
  WorkStealingBag marking_bag;
  LocalWorkStealingBag local_marking_bag(&marking_bag, 0);
  HeapObject* object1 = new HeapObject();
  HeapObject* object2 = nullptr;
  EXPECT_TRUE(local_marking_bag.Push(object1));
  EXPECT_FALSE(local_marking_bag.IsLocalEmpty());
  EXPECT_TRUE(local_marking_bag.Pop(&object2));
  EXPECT_EQ(object1, object2);
  delete object1;
}

}  // namespace internal
}  // namespace v8
