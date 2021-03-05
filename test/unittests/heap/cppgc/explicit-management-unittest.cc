// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/explicit-management.h"

#include "include/cppgc/garbage-collected.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-space.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/sweeper.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

using ExplicitManagementTest = testing::TestSupportingAllocationOnly;

namespace {

class DynamicallySized final : public GarbageCollected<DynamicallySized> {
 public:
  void Trace(Visitor*) const {}
};

size_t AllocatedObjectSize(HeapBase* heap) {
  heap->stats_collector()->NotifySafePointForTesting();
  return heap->stats_collector()->allocated_object_size();
}

}  // namespace

TEST_F(ExplicitManagementTest, FreeRegularObjectToLAB) {
  auto* o =
      MakeGarbageCollected<DynamicallySized>(GetHeap()->GetAllocationHandle());
  const auto* page = BasePage::FromPayload(o);
  auto* heap = page->heap();
  const auto* space = NormalPageSpace::From(page->space());
  const auto& lab = space->linear_allocation_buffer();
  auto& header = HeapObjectHeader::FromPayload(o);
  const size_t size = header.GetSize();
  Address needle = reinterpret_cast<Address>(&header);
  // Test checks freeing to LAB.
  ASSERT_EQ(lab.start(), header.PayloadEnd());
  const size_t lab_size_before_free = lab.size();
  const size_t allocated_size_before = AllocatedObjectSize(heap);
  subtle::FreeUnreferencedObject(o);
  EXPECT_EQ(lab.start(), reinterpret_cast<Address>(needle));
  EXPECT_EQ(lab_size_before_free + size, lab.size());
  // LAB is included in allocated object size, so no change is expected.
  EXPECT_EQ(allocated_size_before, AllocatedObjectSize(heap));
  EXPECT_FALSE(space->free_list().ContainsForTesting({needle, size}));
}

TEST_F(ExplicitManagementTest, FreeRegularObjectToFreeList) {
  auto* o =
      MakeGarbageCollected<DynamicallySized>(GetHeap()->GetAllocationHandle());
  const auto* page = BasePage::FromPayload(o);
  auto* heap = page->heap();
  const auto* space = NormalPageSpace::From(page->space());
  const auto& lab = space->linear_allocation_buffer();
  auto& header = HeapObjectHeader::FromPayload(o);
  const size_t size = header.GetSize();
  Address needle = reinterpret_cast<Address>(&header);
  // Test checks freeing to free list.
  page->heap()->object_allocator().ResetLinearAllocationBuffers();
  ASSERT_EQ(lab.start(), nullptr);
  heap->stats_collector()->NotifySafePointForTesting();
  const size_t allocated_size_before = AllocatedObjectSize(heap);
  subtle::FreeUnreferencedObject(o);
  EXPECT_EQ(lab.start(), nullptr);
  EXPECT_EQ(allocated_size_before - size, AllocatedObjectSize(heap));
  EXPECT_TRUE(space->free_list().ContainsForTesting({needle, size}));
}

TEST_F(ExplicitManagementTest, FreeLargeObject) {
  auto* o = MakeGarbageCollected<DynamicallySized>(
      GetHeap()->GetAllocationHandle(),
      AdditionalBytes(kLargeObjectSizeThreshold));
  const auto* page = BasePage::FromPayload(o);
  auto* heap = page->heap();
  ASSERT_TRUE(page->is_large());
  ConstAddress needle = reinterpret_cast<ConstAddress>(o);
  const size_t size = LargePage::From(page)->PayloadSize();
  EXPECT_TRUE(heap->page_backend()->Lookup(needle));
  const size_t allocated_size_before = AllocatedObjectSize(heap);
  subtle::FreeUnreferencedObject(o);
  EXPECT_FALSE(heap->page_backend()->Lookup(needle));
  EXPECT_EQ(allocated_size_before - size, AllocatedObjectSize(heap));
}

TEST_F(ExplicitManagementTest, FreeBailsOutDuringGC) {
  auto* o =
      MakeGarbageCollected<DynamicallySized>(GetHeap()->GetAllocationHandle());
  const auto* page = BasePage::FromPayload(o);
  auto* heap = page->heap();
  heap->SetInAtomicPauseForTesting(true);
  EXPECT_FALSE(subtle::TryFree(o));
  heap->SetInAtomicPauseForTesting(false);
  EXPECT_TRUE(subtle::TryFree(o));
}

TEST_F(ExplicitManagementTest, FreeNull) {
  DynamicallySized* o = nullptr;
  auto* heap = Heap::From(GetHeap());
  heap->SetInAtomicPauseForTesting(true);
  EXPECT_TRUE(subtle::TryFree(o));
  heap->SetInAtomicPauseForTesting(false);
  EXPECT_TRUE(subtle::TryFree(o));
}

}  // namespace internal
}  // namespace cppgc
