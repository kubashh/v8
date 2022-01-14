// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/virtual-address-space.h"

#include "src/base/emulated-virtual-address-subspace.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace base {

constexpr size_t KB = 1024;
constexpr size_t MB = KB * 1024;

void TestRandomPageAddressGeneration(v8::VirtualAddressSpace* space) {
  space->SetRandomSeed(::testing::FLAGS_gtest_random_seed);
  for (int i = 0; i < 10; i++) {
    Address addr = space->RandomPageAddress();
    CHECK_GE(addr, space->base());
    CHECK_LT(addr, space->base() + space->size());
  }
}

void TestBasicPageAllocation(v8::VirtualAddressSpace* space) {
  // In multiples of the allocation_granularity.
  const size_t allocation_sizes[] = {1, 2, 3, 4, 5, 8, 16, 32, 64};

  std::vector<Address> allocations;
  size_t alignment = space->allocation_granularity();
  for (size_t i = 0; i < arraysize(allocation_sizes); i++) {
    size_t size = allocation_sizes[i] * space->allocation_granularity();
    Address allocation =
        space->AllocatePages(VirtualAddressSpace::kNoHint, size, alignment,
                             PagePermissions::kReadWrite);

    CHECK(allocation);
    CHECK(allocation >= space->base());
    CHECK(allocation < space->base() + space->size());
    allocations.push_back(allocation);

    // Memory must be writable
    memset(reinterpret_cast<void*>(allocation), 0x42, size);
    //... and readable
    CHECK_EQ(0, memcmp(reinterpret_cast<void*>(allocation), "BBBB", 4));
  }

  for (size_t i = 0; i < arraysize(allocation_sizes); i++) {
    Address allocation = allocations[i];
    size_t size = allocation_sizes[i] * space->allocation_granularity();
    CHECK(space->FreePages(allocation, size));
  }
}

void TestPageAllocationAlignment(v8::VirtualAddressSpace* space) {
  // In multiples of the allocation_granularity.
  const size_t alignments[] = {1, 2, 4, 8, 16, 32, 64};
  const size_t size = space->allocation_granularity();

  for (size_t i = 0; i < arraysize(alignments); i++) {
    size_t alignment = alignments[i] * space->allocation_granularity();
    Address allocation =
        space->AllocatePages(VirtualAddressSpace::kNoHint, size, alignment,
                             PagePermissions::kReadWrite);

    CHECK(allocation);
    CHECK_EQ(0, allocation % alignment);
    CHECK(allocation >= space->base());
    CHECK(allocation < space->base() + space->size());

    CHECK(space->FreePages(allocation, size));
  }
}

void TestParentSpaceCannotAllocateInChildSpace(v8::VirtualAddressSpace* parent,
                                               v8::VirtualAddressSpace* child) {
  child->SetRandomSeed(::testing::FLAGS_gtest_random_seed);

  size_t chunksize = parent->allocation_granularity();
  size_t alignment = chunksize;
  Address start = child->base();
  Address end = start + child->size();

  for (int i = 0; i < 10; i++) {
    Address hint = child->RandomPageAddress();
    Address allocation = parent->AllocatePages(hint, chunksize, alignment,
                                               PagePermissions::kNoAccess);
    CHECK(allocation);
    CHECK(allocation < start || allocation >= end);
    CHECK(parent->FreePages(allocation, chunksize));
  }
}

TEST(VirtualAddressSpaceTest, TestRootSpace) {
  VirtualAddressSpace rootspace;

  TestRandomPageAddressGeneration(&rootspace);
  TestBasicPageAllocation(&rootspace);
  TestPageAllocationAlignment(&rootspace);
}

TEST(VirtualAddressSpaceTest, TestSubspace) {
  constexpr size_t kSubspaceSize = 32 * MB;
  constexpr size_t kSubSubspaceSize = 16 * MB;

  VirtualAddressSpace rootspace;

  if (!rootspace.CanAllocateSubspaces()) return;
  size_t subspace_alignment = rootspace.allocation_granularity();
  auto subspace = rootspace.AllocateSubspace(
      VirtualAddressSpace::kNoHint, kSubspaceSize, subspace_alignment,
      PagePermissions::kReadWriteExecute);
  CHECK(subspace);
  CHECK_NE(0, subspace->base());
  CHECK_EQ(kSubspaceSize, subspace->size());

  TestRandomPageAddressGeneration(subspace.get());
  TestBasicPageAllocation(subspace.get());
  TestPageAllocationAlignment(subspace.get());
  TestParentSpaceCannotAllocateInChildSpace(&rootspace, subspace.get());

  // Test sub-subspaces
  if (!subspace->CanAllocateSubspaces()) return;
  size_t subsubspace_alignment = subspace->allocation_granularity();
  auto subsubspace = subspace->AllocateSubspace(
      VirtualAddressSpace::kNoHint, kSubSubspaceSize, subsubspace_alignment,
      PagePermissions::kReadWriteExecute);
  CHECK(subsubspace);
  CHECK_NE(0, subsubspace->base());
  CHECK_EQ(kSubSubspaceSize, subsubspace->size());

  TestRandomPageAddressGeneration(subsubspace.get());
  TestBasicPageAllocation(subsubspace.get());
  TestPageAllocationAlignment(subsubspace.get());
  TestParentSpaceCannotAllocateInChildSpace(subspace.get(), subsubspace.get());
}

TEST(VirtualAddressSpaceTest, TestEmulatedSubspace) {
  constexpr size_t kSubspaceSize = 32 * MB;
  // Size chosen so page allocation tests will obtain pages in both the mapped
  // and the unmapped region.
  constexpr size_t kSubspaceReservationSize = 128 * KB;

  VirtualAddressSpace rootspace;

  size_t subspace_alignment = rootspace.allocation_granularity();
  CHECK_EQ(0, kSubspaceReservationSize % rootspace.allocation_granularity());
  Address reservation = kNullAddress;
  for (int i = 0; i < 10; i++) {
    // Reserve the full size first, then free it again to ensure that there's
    // enough free space behind the final reservation.
    reservation = rootspace.AllocatePages(
        VirtualAddressSpace::kNoHint, kSubspaceSize,
        rootspace.allocation_granularity(), PagePermissions::kNoAccess);
    CHECK(reservation);
    Address hint = reservation;
    CHECK(rootspace.FreePages(reservation, kSubspaceSize));
    reservation =
        rootspace.AllocatePages(hint, kSubspaceReservationSize,
                                subspace_alignment, PagePermissions::kNoAccess);
    if (reservation == hint) {
      break;
    } else {
      CHECK(rootspace.FreePages(reservation, kSubspaceReservationSize));
      reservation = kNullAddress;
    }
  }
  CHECK(reservation);

  EmulatedVirtualAddressSubspace subspace(
      &rootspace, reservation, kSubspaceReservationSize, kSubspaceSize);
  CHECK_EQ(reservation, subspace.base());
  CHECK_EQ(kSubspaceSize, subspace.size());

  TestRandomPageAddressGeneration(&subspace);
  TestBasicPageAllocation(&subspace);
  TestPageAllocationAlignment(&subspace);
  // An emulated subspace does *not* guarantee that the parent space cannot
  // allocate pages in it, so no TestParentSpaceCannotAllocateInChildSpace.
}

}  // namespace base
}  // namespace v8
