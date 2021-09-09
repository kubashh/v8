// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/spaces.h"
#include "test/unittests/heap/bitmap-test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

const uint32_t kBlackCell = 0xAAAAAAAA;
const uint32_t kWhiteCell = 0x00000000;
const uint32_t kBlackByte = 0xAA;
const uint32_t kWhiteByte = 0x00;
const size_t kFirstBit = Bitmap::FirstCellIndex() * Bitmap::kBitsPerCell;

template <typename T>
using BitmapTest = TestWithBitmap<T>;

TYPED_TEST_SUITE(BitmapTest, BitmapTypes);

using NonAtomicBitmapTest =
    TestWithBitmap<ConcurrentBitmap<AccessMode::NON_ATOMIC>>;

TEST_F(NonAtomicBitmapTest, IsZeroInitialized) {
  // We require all tests to start from a zero-initialized bitmap. Manually
  // verify this invariant here.
  for (size_t i = 0; i < Bitmap::kSize; i++) {
    EXPECT_EQ(raw_bitmap()[i], kWhiteByte);
  }
}

TEST_F(NonAtomicBitmapTest, Cells) {
  auto bm = bitmap();
  bm->cells()[Bitmap::FirstCellIndex() + 1] = kBlackCell;
  uint8_t* raw = raw_bitmap();
  int second_cell_base = Bitmap::kBytesPerCell;
  for (size_t i = 0; i < Bitmap::kBytesPerCell; i++) {
    EXPECT_EQ(raw[second_cell_base + i], kBlackByte);
  }
}

TEST_F(NonAtomicBitmapTest, CellsCount) {
  bitmap()->cells()[Bitmap::LastCellIndex()] = kBlackCell;
  // Manually verify on raw memory.
  uint8_t* raw = raw_bitmap();
  for (size_t i = 0; i < Bitmap::kSize; i++) {
    // Last cell should be set.
    if (i >= (Bitmap::kSize - Bitmap::kBytesPerCell)) {
      EXPECT_EQ(raw[i], kBlackByte);
    } else {
      EXPECT_EQ(raw[i], kWhiteByte);
    }
  }
}

TEST_F(NonAtomicBitmapTest, IsClean) {
  auto bm = bitmap();
  EXPECT_TRUE(bm->IsClean());
  bm->cells()[Bitmap::FirstCellIndex()] = kBlackCell;
  EXPECT_FALSE(bm->IsClean());
}

TYPED_TEST(BitmapTest, Clear) {
  auto bm = this->bitmap();
  for (size_t i = 0; i < Bitmap::kSize; i++) {
    this->raw_bitmap()[i] = 0xFFu;
  }
  bm->Clear();
  for (size_t i = 0; i < Bitmap::kSize; i++) {
    EXPECT_EQ(this->raw_bitmap()[i], 0);
  }
}

TYPED_TEST(BitmapTest, MarkAllBits) {
  auto bm = this->bitmap();
  bm->MarkAllBits();
  for (size_t i = 0; i < Bitmap::kSize; i++) {
    EXPECT_EQ(this->raw_bitmap()[i], 0xFF);
  }
}

TYPED_TEST(BitmapTest, ClearRange1) {
  auto bm = this->bitmap();
  bm->cells()[Bitmap::FirstCellIndex() + 0] = kBlackCell;
  bm->cells()[Bitmap::FirstCellIndex() + 1] = kBlackCell;
  bm->cells()[Bitmap::FirstCellIndex() + 2] = kBlackCell;
  bm->ClearRange(kFirstBit,
                 kFirstBit + Bitmap::kBitsPerCell + Bitmap::kBitsPerCell / 2);
  EXPECT_EQ(bm->cells()[Bitmap::FirstCellIndex() + 0], kWhiteCell);
  EXPECT_EQ(bm->cells()[Bitmap::FirstCellIndex() + 1], 0xAAAA0000);
  EXPECT_EQ(bm->cells()[Bitmap::FirstCellIndex() + 2], kBlackCell);
}

TYPED_TEST(BitmapTest, ClearRange2) {
  auto bm = this->bitmap();
  bm->cells()[Bitmap::FirstCellIndex() + 0] = kBlackCell;
  bm->cells()[Bitmap::FirstCellIndex() + 1] = kBlackCell;
  bm->cells()[Bitmap::FirstCellIndex() + 2] = kBlackCell;
  bm->ClearRange(kFirstBit + Bitmap::kBitsPerCell,
                 kFirstBit + Bitmap::kBitsPerCell + Bitmap::kBitsPerCell / 2);
  EXPECT_EQ(bm->cells()[Bitmap::FirstCellIndex() + 0], kBlackCell);
  EXPECT_EQ(bm->cells()[Bitmap::FirstCellIndex() + 1], 0xAAAA0000);
  EXPECT_EQ(bm->cells()[Bitmap::FirstCellIndex() + 2], kBlackCell);
}

TYPED_TEST(BitmapTest, SetAndClearRange) {
  auto bm = this->bitmap();
  for (int i = 0; i < 3; i++) {
    bm->SetRange(kFirstBit + i, kFirstBit + Bitmap::kBitsPerCell + i);
    CHECK_EQ(bm->cells()[Bitmap::FirstCellIndex() + 0], 0xFFFFFFFFu << i);
    CHECK_EQ(bm->cells()[Bitmap::FirstCellIndex() + 1], (1u << i) - 1);
    bm->ClearRange(kFirstBit + i, kFirstBit + Bitmap::kBitsPerCell + i);
    CHECK_EQ(bm->cells()[Bitmap::FirstCellIndex() + 0], 0x0u);
    CHECK_EQ(bm->cells()[Bitmap::FirstCellIndex() + 1], 0x0u);
  }
}

// AllBitsSetInRange() and AllBitsClearInRange() are only used when verifying
// the heap on the main thread so they don't have atomic implementations.
TEST_F(NonAtomicBitmapTest, ClearMultipleRanges) {
  auto bm = this->bitmap();

  bm->SetRange(kFirstBit, kFirstBit + Bitmap::kBitsPerCell * 3);
  CHECK(bm->AllBitsSetInRange(kFirstBit, kFirstBit + Bitmap::kBitsPerCell));

  bm->ClearRange(kFirstBit + Bitmap::kBitsPerCell / 2,
                 kFirstBit + Bitmap::kBitsPerCell);
  bm->ClearRange(kFirstBit + Bitmap::kBitsPerCell,
                 kFirstBit + Bitmap::kBitsPerCell + Bitmap::kBitsPerCell / 2);
  bm->ClearRange(kFirstBit + Bitmap::kBitsPerCell * 2 + 8,
                 kFirstBit + Bitmap::kBitsPerCell * 2 + 16);
  bm->ClearRange(kFirstBit + Bitmap::kBitsPerCell * 2 + 24,
                 kFirstBit + Bitmap::kBitsPerCell * 3);

  CHECK_EQ(bm->cells()[Bitmap::FirstCellIndex()], 0xFFFFu);
  CHECK(bm->AllBitsSetInRange(kFirstBit, kFirstBit + Bitmap::kBitsPerCell / 2));
  CHECK(bm->AllBitsClearInRange(kFirstBit + Bitmap::kBitsPerCell / 2,
                                kFirstBit + Bitmap::kBitsPerCell));

  CHECK_EQ(bm->cells()[Bitmap::FirstCellIndex() + 1], 0xFFFF0000u);
  CHECK(bm->AllBitsClearInRange(
      kFirstBit + Bitmap::kBitsPerCell,
      kFirstBit + Bitmap::kBitsPerCell + Bitmap::kBitsPerCell / 2));
  CHECK(bm->AllBitsSetInRange(
      kFirstBit + Bitmap::kBitsPerCell + Bitmap::kBitsPerCell / 2,
      kFirstBit + Bitmap::kBitsPerCell * 2));

  CHECK_EQ(bm->cells()[Bitmap::FirstCellIndex() + 2], 0xFF00FFu);
  CHECK(bm->AllBitsSetInRange(
      kFirstBit + Bitmap::kBitsPerCell * 2,
      kFirstBit + Bitmap::kBitsPerCell * 2 + Bitmap::kBitsPerCell / 4));
  CHECK(bm->AllBitsClearInRange(
      kFirstBit + Bitmap::kBitsPerCell * 2 + Bitmap::kBitsPerCell / 4,
      kFirstBit + Bitmap::kBitsPerCell * 2 + Bitmap::kBitsPerCell / 2));
  CHECK(bm->AllBitsSetInRange(
      kFirstBit + Bitmap::kBitsPerCell * 2 + Bitmap::kBitsPerCell / 2,
      kFirstBit + Bitmap::kBitsPerCell * 2 + Bitmap::kBitsPerCell / 2 +
          Bitmap::kBitsPerCell / 4));
  CHECK(bm->AllBitsClearInRange(kFirstBit + Bitmap::kBitsPerCell * 2 +
                                    Bitmap::kBitsPerCell / 2 +
                                    Bitmap::kBitsPerCell / 4,
                                kFirstBit + Bitmap::kBitsPerCell * 3));
}

}  // namespace internal
}  // namespace v8
