// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/region-allocator.h"
#include "test/unittests/test-utils.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace base {

using Address = RegionAllocator::Address;
using v8::internal::KB;
using v8::internal::MB;

class RegionAllocatorTest : public ::testing::TestWithParam<int> {};

namespace {

void AllocateWhole(RegionAllocator& ra, size_t page_size, size_t step) {
  const Address end = ra.end();
  for (Address address = ra.begin(); address < end; address += step) {
    CHECK(ra.AllocateRegionAt(address, page_size));
  }
}

}  // namespace

TEST(RegionAllocatorTest, SimpleAllocateRegionAt) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 16;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);
  const Address kEnd = kBegin + kSize;

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region.
  for (Address address = kBegin; address < kEnd; address += kPageSize) {
    CHECK_EQ(ra.free_size(), kEnd - address);
    CHECK(ra.AllocateRegionAt(address, kPageSize));
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Free one region and then the allocation should succeed.
  CHECK_EQ(ra.FreeRegionAt(kBegin), kPageSize);
  CHECK_EQ(ra.free_size(), kPageSize);
  CHECK(ra.AllocateRegionAt(kBegin, kPageSize));

  // Free all the pages.
  for (Address address = kBegin; address < kEnd; address += kPageSize) {
    CHECK_EQ(ra.FreeRegionAt(address), kPageSize);
  }

  // Check that the whole region is free and can be fully allocated.
  CHECK_EQ(ra.free_size(), kSize);
  CHECK_EQ(ra.AllocateRegion(kSize), kBegin);
}

TEST(RegionAllocatorTest, SimpleAllocateRegion) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 1;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);
  const Address kEnd = kBegin + kSize;

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region.
  for (size_t i = 0; i < kPageCount; i++) {
    CHECK_EQ(ra.free_size(), kSize - kPageSize * i);
    Address address = ra.AllocateRegion(kPageSize);
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK_EQ(address, kBegin + kPageSize * i);
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Try to free one page and ensure that we are able to allocate it again.
  for (Address address = kBegin; address < kEnd; address += kPageSize) {
    CHECK_EQ(ra.FreeRegionAt(address), kPageSize);
    CHECK_EQ(ra.AllocateRegion(kPageSize), address);
  }
  CHECK_EQ(ra.free_size(), 0);
}

TEST(RegionAllocatorTest, AllocateRegionAligned) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCountLog = 4;
  const size_t kPageCount = (size_t{1} << kPageCountLog);
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = 0;  // kSize;

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Perform aligned allocations till the whole region is used.
  CHECK_EQ(ra.AllocateRegion(kPageSize), kBegin);
  for (size_t i = 0; i < kPageCountLog; i++) {
    size_t alignment = kPageSize << i;
    Address address = ra.AllocateRegion(alignment, alignment);
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK(IsAligned(address, alignment));
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Try to free one page and ensure that we are able to allocate it again.
  for (size_t i = 0; i < kPageCountLog; i++) {
    size_t alignment = kPageSize << i;
    Address address = kBegin + alignment;
    CHECK_EQ(ra.FreeRegionAt(address), alignment);
    CHECK_EQ(ra.AllocateRegion(address), address);
  }
  CHECK_EQ(ra.free_size(), 0);
}

TEST_P(RegionAllocatorTest, AllocateRegionRandom) {
  const size_t kPageSize = 8 * KB;
  const size_t kPageCountLog = 16;
  const size_t kPageCount = (size_t{1} << kPageCountLog);
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(153 * MB);
  const Address kEnd = kBegin + kSize;

  base::RandomNumberGenerator rng(GetParam());
  RegionAllocator ra(kBegin, kSize, kPageSize);

  std::set<Address> allocated_pages;
  // The page addresses must be randomized this number of allocated pages.
  const size_t kRandomizationLimit = ra.max_load_for_randomization_ / kPageSize;
  CHECK_LT(kRandomizationLimit, kPageCount);

  Address last_address = kBegin;
  bool saw_randomized_pages = false;

  for (size_t i = 0; i < kPageCount; i++) {
    Address address = ra.AllocateRegion(&rng, kPageSize);
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK(IsAligned(address, kPageSize));
    CHECK_LE(kBegin, address);
    CHECK_LT(address, kEnd);
    CHECK_EQ(allocated_pages.find(address), allocated_pages.end());
    allocated_pages.insert(address);

    saw_randomized_pages |= (address < last_address);
    last_address = address;

    if (i == kRandomizationLimit) {
      // We must evidence allocation randomization till this point.
      // The rest of the allocations may still be randomized depending on
      // the free ranges distribution, however it is not guaranteed.
      CHECK(saw_randomized_pages);
    }
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);
}

TEST(RegionAllocatorTest, AllocateBigRegions) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCountLog = 10;
  const size_t kPageCount = (size_t{1} << kPageCountLog) - 1;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region.
  for (size_t i = 0; i < kPageCountLog; i++) {
    Address address = ra.AllocateRegion(kPageSize * (size_t{1} << i));
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK_EQ(address, kBegin + kPageSize * ((size_t{1} << i) - 1));
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Try to free one page and ensure that we are able to allocate it again.
  for (size_t i = 0; i < kPageCountLog; i++) {
    const size_t size = kPageSize * (size_t{1} << i);
    Address address = kBegin + kPageSize * ((size_t{1} << i) - 1);
    CHECK_EQ(ra.FreeRegionAt(address), size);
    CHECK_EQ(ra.AllocateRegion(size), address);
  }
  CHECK_EQ(ra.free_size(), 0);
}

TEST(RegionAllocatorTest, MergeLeftToRightCoalecsingRegions) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCountLog = 10;
  const size_t kPageCount = (size_t{1} << kPageCountLog);
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region using the following page size pattern:
  // |0|1|22|3333|...
  CHECK_EQ(ra.AllocateRegion(kPageSize), kBegin);
  for (size_t i = 0; i < kPageCountLog; i++) {
    Address address = ra.AllocateRegion(kPageSize * (size_t{1} << i));
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK_EQ(address, kBegin + kPageSize * (size_t{1} << i));
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Try to free two coalescing regions and ensure the new page of bigger size
  // can be allocated.
  size_t current_size = kPageSize;
  for (size_t i = 0; i < kPageCountLog; i++) {
    CHECK_EQ(ra.FreeRegionAt(kBegin), current_size);
    CHECK_EQ(ra.FreeRegionAt(kBegin + current_size), current_size);
    current_size += current_size;
    CHECK_EQ(ra.AllocateRegion(current_size), kBegin);
  }
  CHECK_EQ(ra.free_size(), 0);
}

TEST_P(RegionAllocatorTest, MergeRightToLeftCoalecsingRegions) {
  base::RandomNumberGenerator rng(GetParam());
  const size_t kPageSize = 4 * KB;
  const size_t kPageCountLog = 10;
  const size_t kPageCount = (size_t{1} << kPageCountLog);
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region.
  for (size_t i = 0; i < kPageCount; i++) {
    Address address = ra.AllocateRegion(kPageSize);
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK_EQ(address, kBegin + kPageSize * i);
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Free pages with even indices left-to-right.
  for (size_t i = 0; i < kPageCount; i += 2) {
    Address address = kBegin + kPageSize * i;
    CHECK_EQ(ra.FreeRegionAt(address), kPageSize);
  }

  // Free pages with odd indices right-to-left.
  for (size_t i = 1; i < kPageCount; i += 2) {
    Address address = kBegin + kPageSize * (kPageCount - i);
    CHECK_EQ(ra.FreeRegionAt(address), kPageSize);
    // Now we should be able to allocate a double-sized page.
    CHECK_EQ(ra.AllocateRegion(kPageSize * 2), address - kPageSize);
    // .. but there's a window for only one such page.
    CHECK_EQ(ra.AllocateRegion(kPageSize * 2),
             RegionAllocator::kAllocationFailure);
  }

  // Free all the double-sized pages.
  for (size_t i = 0; i < kPageCount; i += 2) {
    Address address = kBegin + kPageSize * i;
    CHECK_EQ(ra.FreeRegionAt(address), kPageSize * 2);
  }

  // Check that the whole region is free and can be fully allocated.
  CHECK_EQ(ra.free_size(), kSize);
  CHECK_EQ(ra.AllocateRegion(kSize), kBegin);
}

TEST(RegionAllocatorTest, Fragmentation) {
  const size_t kPageSize = 64 * KB;
  const size_t kPageCount = 9;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region.
  for (size_t i = 0; i < kPageCount; i++) {
    Address address = ra.AllocateRegion(kPageSize);
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK_EQ(address, kBegin + kPageSize * i);
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Free pages in the following order and check the freed size.
  struct {
    size_t page_index_to_free;
    size_t expected_page_count;
  } testcase[] = {          // XXXXXXXXX
                  {0, 9},   // .XXXXXXXX
                  {2, 9},   // .X.XXXXXX
                  {4, 9},   // .X.X.XXXX
                  {6, 9},   // .X.X.X.XX
                  {8, 9},   // .X.X.X.X.
                  {1, 7},   // ...X.X.X.
                  {7, 5},   // ...X.X...
                  {3, 3},   // .....X...
                  {5, 1}};  // .........
  CHECK_EQ(kPageCount, arraysize(testcase));

  CHECK_EQ(ra.all_regions_.size(), kPageCount);
  for (size_t i = 0; i < kPageCount; i++) {
    Address address = kBegin + kPageSize * testcase[i].page_index_to_free;
    CHECK_EQ(ra.FreeRegionAt(address), kPageSize);
    CHECK_EQ(ra.all_regions_.size(), testcase[i].expected_page_count);
  }

  // Check that the whole region is free and can be fully allocated.
  CHECK_EQ(ra.free_size(), kSize);
  CHECK_EQ(ra.AllocateRegion(kSize), kBegin);
}

TEST(RegionAllocatorTest, FindRegion) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 16;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);
  const Address kEnd = kBegin + kSize;

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region.
  for (Address address = kBegin; address < kEnd; address += kPageSize) {
    CHECK_EQ(ra.free_size(), kEnd - address);
    CHECK(ra.AllocateRegionAt(address, kPageSize));
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // The out-of region requests must return end iterator.
  CHECK_EQ(ra.FindRegion(kBegin - 1), ra.all_regions_.end());
  CHECK_EQ(ra.FindRegion(kBegin - kPageSize), ra.all_regions_.end());
  CHECK_EQ(ra.FindRegion(kBegin / 2), ra.all_regions_.end());
  CHECK_EQ(ra.FindRegion(kEnd), ra.all_regions_.end());
  CHECK_EQ(ra.FindRegion(kEnd + kPageSize), ra.all_regions_.end());
  CHECK_EQ(ra.FindRegion(kEnd * 2), ra.all_regions_.end());

  for (Address address = kBegin; address < kEnd; address += kPageSize / 4) {
    RegionAllocator::AllRegionsSet::iterator region_iter =
        ra.FindRegion(address);
    CHECK_NE(region_iter, ra.all_regions_.end());
    RegionAllocator::Region* region = *region_iter;
    Address region_start = RoundDown(address, kPageSize);
    CHECK_EQ(region->begin(), region_start);
    CHECK_LE(region->begin(), address);
    CHECK_LT(address, region->end());
  }
}

TEST(RegionAllocatorTest, FreeRegionWhole) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 32;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  {
    RegionAllocator ra(kBegin, kSize, kPageSize);
    // Allocate the whole region.
    AllocateWhole(ra, kPageSize, kPageSize);
    CHECK_EQ(ra.free_size(), 0);
    CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);
    CHECK_EQ(ra.all_regions_.size(), kPageCount);

    // Free exactly the whole region.
    ra.FreeRegion(kBegin, kSize);
    CHECK_EQ(ra.free_size(), kSize);
    CHECK_EQ(ra.all_regions_.size(), 1);
    CHECK_EQ(ra.AllocateRegion(kSize), kBegin);
    CHECK_EQ(ra.FreeRegionAt(kBegin), kSize);
  }

  {
    RegionAllocator ra(kBegin, kSize, kPageSize);
    // Allocate the whole region.
    AllocateWhole(ra, kPageSize, kPageSize);
    CHECK_EQ(ra.free_size(), 0);
    CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);
    CHECK_EQ(ra.all_regions_.size(), kPageCount);

    // Free the whole address space.
    ra.FreeRegion(kPageSize, size_t{-kPageSize});
    CHECK_EQ(ra.free_size(), kSize);
    CHECK_EQ(ra.all_regions_.size(), 1);
    CHECK_EQ(ra.AllocateRegion(kSize), kBegin);
    CHECK_EQ(ra.FreeRegionAt(kBegin), kSize);
  }
}

TEST(RegionAllocatorTest, FreeRegionMultipleUsed) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 32;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);
  // Allocate the whole region.
  // XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
  // 0123456789abcdef0123456789abcdef
  AllocateWhole(ra, kPageSize, kPageSize);
  size_t expected_page_count = kPageCount;
  size_t expected_free_size = 0;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), expected_page_count);

  // Free up some holes there.
  const Address free_region1_address = kBegin + kPageSize * 2;
  const size_t free_region1_size = kPageSize * 5;
  //  _____
  // XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
  // 0123456789abcdef0123456789abcdef
  ra.FreeRegion(free_region1_address, free_region1_size);
  // X.....XXXXXXXXXXXXXXXXXXXXXXXXXX
  // 0123456789abcdef0123456789abcdef
  expected_page_count -= 5 - 1;
  expected_free_size += free_region1_size;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), expected_page_count);

  const Address free_region2_address = kBegin + kPageSize * 17;
  const size_t free_region2_size = kPageSize * 7;
  //                  _______
  // X.....XXXXXXXXXXXXXXXXXXXXXXXXXX
  // 0123456789abcdef0123456789abcdef
  ra.FreeRegion(free_region2_address, free_region2_size);
  // X.....XXXXXXXXXXX.......XXXXXXXX
  // 0123456789abcdef0123456789abcdef
  expected_page_count -= 7 - 1;
  expected_free_size += free_region2_size;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), expected_page_count);

  // Ensure we can allocate in the freed regions.
  CHECK(ra.AllocateRegionAt(free_region1_address, free_region1_size));
  CHECK(ra.AllocateRegionAt(free_region2_address, free_region2_size));
  expected_free_size = 0;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), expected_page_count);
}

TEST(RegionAllocatorTest, FreeRegionInsideUsed) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 32;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);
  // Mark the whole region as used.
  CHECK(ra.AllocateRegionAt(kBegin, kSize));
  // XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
  // 0123456789abcdef0123456789abcdef
  size_t expected_page_count = 1;
  size_t expected_free_size = 0;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), expected_page_count);

  // Free up some holes there.
  const Address free_region1_address = kBegin + kPageSize * 2;
  const size_t free_region1_size = kPageSize * 5;
  //   _____
  // XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
  // 0123456789abcdef0123456789abcdef
  ra.FreeRegion(free_region1_address, free_region1_size);
  // XX.....XXXXXXXXXXXXXXXXXXXXXXXXX
  // 0123456789abcdef0123456789abcdef
  expected_page_count += 2;
  expected_free_size += free_region1_size;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), expected_page_count);

  const Address free_region2_address = kBegin + kPageSize * 17;
  const size_t free_region2_size = kPageSize * 6;
  //                  ______
  // XX.....XXXXXXXXXXXXXXXXXXXXXXXXX
  // 0123456789abcdef0123456789abcdef
  ra.FreeRegion(free_region2_address, free_region2_size);
  // XX.....XXXXXXXXXX......XXXXXXXXX
  // 0123456789abcdef0123456789abcdef
  expected_page_count += 2;
  expected_free_size += free_region2_size;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), expected_page_count);

  const Address free_region3_address = kBegin;
  const size_t free_region3_size = kPageSize;
  // _
  // XX.....XXXXXXXXXX......XXXXXXXXX
  // 0123456789abcdef0123456789abcdef
  ra.FreeRegion(free_region3_address, free_region3_size);
  // .X.....XXXXXXXXXX......XXXXXXXXX
  // 0123456789abcdef0123456789abcdef
  expected_page_count += 1;
  expected_free_size += free_region3_size;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), expected_page_count);

  const Address free_region4_address = kBegin + kPageSize * 27;
  const size_t free_region4_size = kPageSize * 5;
  //                            _____
  // .X.....XXXXXXXXXX......XXXXXXXXX
  // 0123456789abcdef0123456789abcdef
  ra.FreeRegion(free_region4_address, free_region4_size);
  // .X.....XXXXXXXXXX......XXXX.....
  // 0123456789abcdef0123456789abcdef
  expected_page_count += 1;
  expected_free_size += free_region4_size;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), expected_page_count);

  // Ensure we can allocate in the freed regions.
  CHECK(ra.AllocateRegionAt(free_region1_address, free_region1_size));
  CHECK(ra.AllocateRegionAt(free_region2_address, free_region2_size));
  CHECK(ra.AllocateRegionAt(free_region3_address, free_region3_size));
  CHECK(ra.AllocateRegionAt(free_region4_address, free_region4_size));
  expected_free_size = 0;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), expected_page_count);
}

TEST(RegionAllocatorTest, FreeRegionSplitMerge) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 32;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);
  // Allocate pages with holes:
  // XXXX...XXXX...XXXX...XXXX...XXXX
  // 0123456789abcdef0123456789abcdef
  AllocateWhole(ra, kPageSize * 4, kPageSize * 7);
  size_t expected_free_size = kPageSize * 3 * 4;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), 9);

  // Free up some holes there.
  const Address free_region1_address = kBegin + kPageSize * 2;
  const size_t free_region1_size = kPageSize * 7;
  //   _______
  // XXXX...XXXX...XXXX...XXXX...XXXX
  // 0123456789abcdef0123456789abcdef
  ra.FreeRegion(free_region1_address, free_region1_size);
  // XX.......XX...XXXX...XXXX...XXXX
  // 0123456789abcdef0123456789abcdef
  expected_free_size += kPageSize * 4;  // 4 pages used were freed
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), 9);

  const Address free_region2_address = kBegin + kPageSize * 11;
  const size_t free_region2_size = kPageSize * 4;
  //            ____
  // XX.......XX...XXXX...XXXX...XXXX
  // 0123456789abcdef0123456789abcdef
  ra.FreeRegion(free_region2_address, free_region2_size);
  // XX.......XX....XXX...XXXX...XXXX
  // 0123456789abcdef0123456789abcdef
  expected_free_size += kPageSize;  // 1 used page was freed
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), 9);

  const Address free_region3_address = kBegin + kPageSize * 17;
  const size_t free_region3_size = kPageSize * 4;
  //                  ____
  // XX.......XX....XXX...XXXX...XXXX
  // 0123456789abcdef0123456789abcdef
  ra.FreeRegion(free_region3_address, free_region3_size);
  // XX.......XX....XX....XXXX...XXXX
  // 0123456789abcdef0123456789abcdef
  expected_free_size += kPageSize;  // 1 used page was freed
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), 9);

  //     ___
  // XX.......XX....XXX...XXXX...XXXX
  // 0123456789abcdef0123456789abcdef
  ra.FreeRegion(kBegin + kPageSize * 4, kPageSize * 3);
  // Nothing has changed.
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), 9);

  const Address free_region4_address = kBegin + kPageSize;
  const size_t free_region4_size = kPageSize * 30;
  //  ______________________________
  // XX.......XX....XXX...XXXX...XXXX
  // 0123456789abcdef0123456789abcdef
  ra.FreeRegion(free_region4_address, free_region4_size);
  // X..............................X
  // 0123456789abcdef0123456789abcdef
  expected_free_size = free_region4_size;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), 3);

  // Ensure we can allocate in the freed regions.
  CHECK(ra.AllocateRegionAt(free_region4_address, free_region4_size));
  expected_free_size = 0;
  CHECK_EQ(ra.free_size(), expected_free_size);
  CHECK_EQ(ra.all_regions_.size(), 3);
}

}  // namespace base
}  // namespace v8
