// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/gc-info.h"

#include "include/cppgc/platform.h"
#include "src/base/page-allocator.h"
#include "src/heap/cppgc/gc-info-table.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

TEST(GCInfoTableTest, InitialEmpty) {
  v8::base::PageAllocator page_allocator;
  GCInfoTable table(&page_allocator);
  EXPECT_EQ(GCInfoTable::kMinIndex, table.NumberOfGCInfosForTesting());
}

TEST(GCInfoTableTest, ResizeToMaxIndex) {
  v8::base::PageAllocator page_allocator;
  GCInfoTable table(&page_allocator);
  GCInfo info = {nullptr, false};
  std::atomic<GCInfoIndex> slot{0};
  for (GCInfoIndex i = GCInfoTable::kMinIndex; i < GCInfoTable::kMaxIndex;
       i++) {
    slot = 0;
    GCInfoIndex index = table.EnsureGCInfoIndex(info, &slot);
    EXPECT_EQ(index, slot);
    EXPECT_LT(0u, slot);
    EXPECT_EQ(&info, &table.GCInfoFromIndex(index));
  }
}

TEST(GCInfoTableDeathTest, MoreThanMaxIndexInfos) {
  v8::base::PageAllocator page_allocator;
  GCInfoTable table(&page_allocator);
  GCInfo info = {nullptr, false};
  std::atomic<GCInfoIndex> slot{0};
  // Create GCInfoTable::kMaxIndex entries.
  for (GCInfoIndex i = GCInfoTable::kMinIndex; i < GCInfoTable::kMaxIndex;
       i++) {
    slot = 0;
    table.EnsureGCInfoIndex(info, &slot);
  }
  slot = 0;
  EXPECT_DEATH(table.EnsureGCInfoIndex(info, &slot), "");
}

TEST(GCInfoTableDeathTest, OldTableAreaIsReadOnly) {
  v8::base::PageAllocator page_allocator;
  GCInfoTable table(&page_allocator);
  GCInfo info = {nullptr, false};
  std::atomic<GCInfoIndex> slot{0};
  // Use up all slots until limit.
  GCInfoIndex limit = table.LimitForTesting();
  for (GCInfoIndex i = GCInfoTable::kMinIndex; i < limit; i++) {
    slot = 0;
    table.EnsureGCInfoIndex(info, &slot);
  }
  EXPECT_EQ(limit, table.LimitForTesting());
  slot = 0;
  table.EnsureGCInfoIndex(info, &slot);
  EXPECT_NE(limit, table.LimitForTesting());
  // Old area is now read-only.
  auto* first_slot = table.TableSlotForTesting(GCInfoTable::kMinIndex);
  EXPECT_DEATH(*first_slot = nullptr, "");
}

// Tests using the global table and GCInfoTrait.

namespace {

class BasicType final {};
class OtherBasicType final {};

}  // namespace

TEST(GCInfoTraitTest, IndexInBounds) {
  v8::base::PageAllocator page_allocator;
  GlobalGCInfoTable::Create(&page_allocator);
  const GCInfoIndex index = GCInfoTrait<BasicType>::Index();
  EXPECT_GT(GCInfoTable::kMaxIndex, index);
  EXPECT_LE(GCInfoTable::kMinIndex, index);
}

TEST(GCInfoTraitTest, TraitReturnsSameIndexForSameType) {
  v8::base::PageAllocator page_allocator;
  GlobalGCInfoTable::Create(&page_allocator);
  const GCInfoIndex index1 = GCInfoTrait<BasicType>::Index();
  const GCInfoIndex index2 = GCInfoTrait<BasicType>::Index();
  EXPECT_EQ(index1, index2);
}

TEST(GCInfoTraitTest, TraitReturnsDifferentIndexForDifferentTypes) {
  v8::base::PageAllocator page_allocator;
  GlobalGCInfoTable::Create(&page_allocator);
  const GCInfoIndex index1 = GCInfoTrait<BasicType>::Index();
  const GCInfoIndex index2 = GCInfoTrait<OtherBasicType>::Index();
  EXPECT_NE(index1, index2);
}

}  // namespace internal
}  // namespace cppgc
