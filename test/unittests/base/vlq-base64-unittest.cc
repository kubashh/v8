// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "src/base/vlq-base64.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace base {

TEST(VLQBASE64, charToDigit) {
  char syms[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(i, charToDigitDecodeForTesting(syms[i]));
  }
}

TEST(VLQBASE64, DecodeOneSegment) {
  size_t pos = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), VLQBase64Decode("", pos));
  pos = 0;
  // Unsupported symbol
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), VLQBase64Decode("*", pos));
  pos = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), VLQBase64Decode("&", pos));
  pos = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), VLQBase64Decode("kt:", pos));
  pos = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), VLQBase64Decode("k^C", pos));
  pos = 0;
  // Imcomplete string
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("kth4yp", pos));
  pos = 0;
  EXPECT_EQ(0, VLQBase64Decode("A", pos));
  EXPECT_EQ(1ul, pos);
  pos = 0;
  EXPECT_EQ(1, VLQBase64Decode("C", pos));
  EXPECT_EQ(1ul, pos);
  pos = 0;
  EXPECT_EQ(12, VLQBase64Decode("Y", pos));
  EXPECT_EQ(1ul, pos);
  pos = 0;
  EXPECT_EQ(123, VLQBase64Decode("2H", pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(1234, VLQBase64Decode("ktC", pos));
  EXPECT_EQ(3ul, pos);
  pos = 0;
  EXPECT_EQ(12345, VLQBase64Decode("yjY", pos));
  EXPECT_EQ(3ul, pos);
  pos = 0;
  EXPECT_EQ(123456, VLQBase64Decode("gkxH", pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(1234567, VLQBase64Decode("uorrC", pos));
  EXPECT_EQ(5ul, pos);
  pos = 0;
  EXPECT_EQ(12345678, VLQBase64Decode("80wxX", pos));
  EXPECT_EQ(5ul, pos);
  pos = 0;
  EXPECT_EQ(123456789, VLQBase64Decode("qxmvrH", pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(1234567890, VLQBase64Decode("kth4ypC", pos));
  EXPECT_EQ(7ul, pos);
  pos = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::max(),
            VLQBase64Decode("+/////D", pos));
  EXPECT_EQ(7ul, pos);
  pos = 0;
  // An overflowed value 12345678901 (0x2DFDC1C35)
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("qjuw7/2A", pos));
  pos = 0;
  // An overflowed value 123456789012(0x1CBE991A14)
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("ohtkz+lH", pos));
  pos = 0;
  EXPECT_EQ(static_cast<int>(0x60000000), VLQBase64Decode("ggggggD", pos));
  pos = 0;
  // An overflowed value 4294967296  (0x100000000)
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("ggggggE", pos));
  pos = 0;
  EXPECT_EQ(-1, VLQBase64Decode("D", pos));
  EXPECT_EQ(1ul, pos);
  pos = 0;
  EXPECT_EQ(-12, VLQBase64Decode("Z", pos));
  EXPECT_EQ(1ul, pos);
  pos = 0;
  EXPECT_EQ(-123, VLQBase64Decode("3H", pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(-1234, VLQBase64Decode("ltC", pos));
  EXPECT_EQ(3ul, pos);
  pos = 0;
  EXPECT_EQ(-12345, VLQBase64Decode("zjY", pos));
  EXPECT_EQ(3ul, pos);
  pos = 0;
  EXPECT_EQ(-123456, VLQBase64Decode("hkxH", pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(-1234567, VLQBase64Decode("vorrC", pos));
  EXPECT_EQ(5ul, pos);
  pos = 0;
  EXPECT_EQ(-12345678, VLQBase64Decode("90wxX", pos));
  EXPECT_EQ(5ul, pos);
  pos = 0;
  EXPECT_EQ(-123456789, VLQBase64Decode("rxmvrH", pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(-1234567890, VLQBase64Decode("lth4ypC", pos));
  EXPECT_EQ(7ul, pos);
  pos = 0;
  EXPECT_EQ(-std::numeric_limits<int32_t>::max(),
            VLQBase64Decode("//////D", pos));
  EXPECT_EQ(7ul, pos);
  pos = 0;
  // An overflowed value -12345678901, |value| = (0x2DFDC1C35)
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("rjuw7/2A", pos));
  pos = 0;
  // An overflowed value -123456789012,|value| = (0x1CBE991A14)
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("phtkz+lH", pos));
  pos = 0;
  EXPECT_EQ(-static_cast<int>(0x60000000), VLQBase64Decode("hgggggD", pos));
  pos = 0;
  // An overflowed value -4294967296,  |value| = (0x100000000)
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("hgggggE", pos));
}

TEST(VLQBASE64, DecodeTwoSegment) {
  size_t pos = 0;
  EXPECT_EQ(0, VLQBase64Decode("AA", pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("AA", pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(5, VLQBase64Decode("KA", pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("KA", pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(0, VLQBase64Decode("AQ", pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(8, VLQBase64Decode("AQ", pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(6, VLQBase64Decode("MG", pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(3, VLQBase64Decode("MG", pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(13, VLQBase64Decode("a4E", pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(76, VLQBase64Decode("a4E", pos));
  EXPECT_EQ(3ul, pos);
  pos = 0;
  EXPECT_EQ(108, VLQBase64Decode("4GyO", pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(233, VLQBase64Decode("4GyO", pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(2048, VLQBase64Decode("ggEqnD", pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(1653, VLQBase64Decode("ggEqnD", pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(65376, VLQBase64Decode("g2/D0ilF", pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(84522, VLQBase64Decode("g2/D0ilF", pos));
  EXPECT_EQ(8ul, pos);
  pos = 0;
  EXPECT_EQ(537798, VLQBase64Decode("ss6gBy0m3B", pos));
  EXPECT_EQ(5ul, pos);
  EXPECT_EQ(904521, VLQBase64Decode("ss6gBy0m3B", pos));
  EXPECT_EQ(10ul, pos);
  pos = 0;
  EXPECT_EQ(-5, VLQBase64Decode("LA", pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("LA", pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(0, VLQBase64Decode("AR", pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(-8, VLQBase64Decode("AR", pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(-6, VLQBase64Decode("NH", pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(-3, VLQBase64Decode("NH", pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(-13, VLQBase64Decode("b5E", pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(-76, VLQBase64Decode("b5E", pos));
  EXPECT_EQ(3ul, pos);
  pos = 0;
  EXPECT_EQ(-108, VLQBase64Decode("5GzO", pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(-233, VLQBase64Decode("5GzO", pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(-2048, VLQBase64Decode("hgErnD", pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(-1653, VLQBase64Decode("hgErnD", pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(-65376, VLQBase64Decode("h2/D1ilF", pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(-84522, VLQBase64Decode("h2/D1ilF", pos));
  EXPECT_EQ(8ul, pos);
  pos = 0;
  EXPECT_EQ(-537798, VLQBase64Decode("ts6gBz0m3B", pos));
  EXPECT_EQ(5ul, pos);
  EXPECT_EQ(-904521, VLQBase64Decode("ts6gBz0m3B", pos));
  EXPECT_EQ(10ul, pos);
  pos = 0;
  EXPECT_EQ(108, VLQBase64Decode("4GzO", pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(-233, VLQBase64Decode("4GzO", pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(2048, VLQBase64Decode("ggErnD", pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(-1653, VLQBase64Decode("ggErnD", pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(65376, VLQBase64Decode("g2/D1ilF", pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(-84522, VLQBase64Decode("g2/D1ilF", pos));
  EXPECT_EQ(8ul, pos);
  pos = 0;
  EXPECT_EQ(537798, VLQBase64Decode("ss6gBz0m3B", pos));
  EXPECT_EQ(5ul, pos);
  EXPECT_EQ(-904521, VLQBase64Decode("ss6gBz0m3B", pos));
  EXPECT_EQ(10ul, pos);
  pos = 0;
  EXPECT_EQ(-108, VLQBase64Decode("5GyO", pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(233, VLQBase64Decode("5GyO", pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(-2048, VLQBase64Decode("hgEqnD", pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(1653, VLQBase64Decode("hgEqnD", pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(-65376, VLQBase64Decode("h2/D0ilF", pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(84522, VLQBase64Decode("h2/D0ilF", pos));
  EXPECT_EQ(8ul, pos);
  pos = 0;
  EXPECT_EQ(-537798, VLQBase64Decode("ts6gBy0m3B", pos));
  EXPECT_EQ(5ul, pos);
  EXPECT_EQ(904521, VLQBase64Decode("ts6gBy0m3B", pos));
  EXPECT_EQ(10ul, pos);
}

TEST(VLQBASE64, DecodeFourSegment) {
  size_t pos = 0;
  EXPECT_EQ(0, VLQBase64Decode("AAAA", pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("AAAA", pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("AAAA", pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("AAAA", pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(8, VLQBase64Decode("QADA", pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("QADA", pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(-1, VLQBase64Decode("QADA", pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("QADA", pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(2, VLQBase64Decode("ECQY", pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(1, VLQBase64Decode("ECQY", pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(8, VLQBase64Decode("ECQY", pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(12, VLQBase64Decode("ECQY", pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(3200, VLQBase64Decode("goGguCioPk9I", pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(1248, VLQBase64Decode("goGguCioPk9I", pos));
  EXPECT_EQ(6ul, pos);
  EXPECT_EQ(7809, VLQBase64Decode("goGguCioPk9I", pos));
  EXPECT_EQ(9ul, pos);
  EXPECT_EQ(4562, VLQBase64Decode("goGguCioPk9I", pos));
  EXPECT_EQ(12ul, pos);
  pos = 0;
  EXPECT_EQ(1021, VLQBase64Decode("6/BACA", pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("6/BACA", pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(1, VLQBase64Decode("6/BACA", pos));
  EXPECT_EQ(5ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("6/BACA", pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(1207, VLQBase64Decode("urCAQA", pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("urCAQA", pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(8, VLQBase64Decode("urCAQA", pos));
  EXPECT_EQ(5ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("urCAQA", pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(54, VLQBase64Decode("sDACA", pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("sDACA", pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(1, VLQBase64Decode("sDACA", pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("sDACA", pos));
  EXPECT_EQ(5ul, pos);
}
}  // namespace base
}  // namespace v8
