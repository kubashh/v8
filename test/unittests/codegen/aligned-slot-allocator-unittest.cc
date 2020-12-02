// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/aligned-slot-allocator.h"

#include "src/base/bits.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace internal {

class AlignedSlotAllocatorUnitTest : public ::testing::Test {
 public:
  AlignedSlotAllocatorUnitTest() = default;
  ~AlignedSlotAllocatorUnitTest() override = default;

  void Allocate(int size, int expected) {
    int next = allocator_.NextSlot(size);
    int result = allocator_.Allocate(size);
    EXPECT_EQ(next, result);            // NextSlot/Allocate are consistent.
    EXPECT_EQ(expected, result);        // result is expected
    EXPECT_EQ(0, result & (size - 1));  // result is aligned to size
    int slot_end = result + static_cast<int>(base::bits::RoundUpToPowerOfTwo32(
                                static_cast<uint32_t>(size)));
    EXPECT_LE(slot_end, allocator_.End());  // allocator end is beyond slot.
  }

  AlignedSlotAllocator allocator_;
};

TEST_F(AlignedSlotAllocatorUnitTest, Allocate1) {
  Allocate(1, 0);
  EXPECT_EQ(2, allocator_.NextSlot(2));
  EXPECT_EQ(4, allocator_.NextSlot(4));

  Allocate(1, 1);
  EXPECT_EQ(2, allocator_.NextSlot(2));
  EXPECT_EQ(4, allocator_.NextSlot(4));

  Allocate(1, 2);
  EXPECT_EQ(4, allocator_.NextSlot(2));
  EXPECT_EQ(4, allocator_.NextSlot(4));

  Allocate(1, 3);
  EXPECT_EQ(4, allocator_.NextSlot(2));
  EXPECT_EQ(4, allocator_.NextSlot(4));

  // Make sure we use 1-fragments.
  Allocate(1, 4);
  Allocate(2, 6);
  Allocate(1, 5);

  // Make sure we use 2-fragments.
  Allocate(2, 8);
  Allocate(1, 10);
  Allocate(1, 11);
}

TEST_F(AlignedSlotAllocatorUnitTest, Allocate2) {
  Allocate(2, 0);
  EXPECT_EQ(2, allocator_.NextSlot(1));
  EXPECT_EQ(4, allocator_.NextSlot(4));

  Allocate(2, 2);
  EXPECT_EQ(4, allocator_.NextSlot(1));
  EXPECT_EQ(4, allocator_.NextSlot(4));

  // Make sure we use 2-fragments.
  Allocate(1, 4);
  Allocate(2, 6);
  Allocate(2, 8);
}

TEST_F(AlignedSlotAllocatorUnitTest, Allocate4) {
  Allocate(4, 0);
  EXPECT_EQ(4, allocator_.NextSlot(1));
  EXPECT_EQ(4, allocator_.NextSlot(2));

  Allocate(1, 4);
  Allocate(4, 8);

  Allocate(2, 6);
  Allocate(4, 12);
}

TEST_F(AlignedSlotAllocatorUnitTest, Reserve) {
  allocator_.Reserve(1);
  EXPECT_EQ(1, allocator_.End());
  EXPECT_EQ(1, allocator_.NextSlot(1));
  EXPECT_EQ(2, allocator_.NextSlot(2));
  EXPECT_EQ(4, allocator_.NextSlot(4));

  allocator_.Reserve(1);
  EXPECT_EQ(2, allocator_.End());
  EXPECT_EQ(2, allocator_.NextSlot(1));
  EXPECT_EQ(2, allocator_.NextSlot(2));
  EXPECT_EQ(4, allocator_.NextSlot(4));

  Allocate(1, 2);

  allocator_.Reserve(2);
  EXPECT_EQ(5, allocator_.End());
  EXPECT_EQ(5, allocator_.NextSlot(1));
  EXPECT_EQ(6, allocator_.NextSlot(2));
  EXPECT_EQ(8, allocator_.NextSlot(4));

  // Advance beyond 1- and 2- fragments.
  Allocate(4, 8);
  EXPECT_EQ(12, allocator_.End());
  EXPECT_EQ(5, allocator_.NextSlot(1));
  EXPECT_EQ(6, allocator_.NextSlot(2));

  // Reserve 0 should end a slot area and discard fragments.
  allocator_.Reserve(0);
  EXPECT_EQ(12, allocator_.End());
  EXPECT_EQ(12, allocator_.NextSlot(1));
  EXPECT_EQ(12, allocator_.NextSlot(2));
  EXPECT_EQ(12, allocator_.NextSlot(4));
}

TEST_F(AlignedSlotAllocatorUnitTest, End) {
  allocator_.Allocate(1);
  EXPECT_EQ(1, allocator_.End());
  // Allocate 2, leaving a fragment at 1. End should be at 4.
  allocator_.Allocate(2);
  EXPECT_EQ(4, allocator_.End());
  // Allocate should consume fragment.
  EXPECT_EQ(1, allocator_.Allocate(1));
  // End should still be 4.
  EXPECT_EQ(4, allocator_.End());
}

}  // namespace internal
}  // namespace v8
