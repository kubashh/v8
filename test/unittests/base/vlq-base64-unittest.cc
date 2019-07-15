// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <limits>

#include "src/base/vlq-base64.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace base {

TEST(VLQBASE64, charToDigit) {
  char kSyms[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  for (int i = 0; i < 256; ++i) {
    char* pos = strchr(kSyms, static_cast<char>(i));
    int8_t expected = i == 0 || pos == nullptr ? -1 : pos - kSyms;
    EXPECT_EQ(expected, charToDigitDecodeForTesting(static_cast<uint8_t>(i)));
  }
}

TEST(VLQBASE64, DecodeOneSegment) {
  size_t pos = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("", strlen(""), &pos));
  pos = 0;
  // Unsupported symbol
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("*", strlen("*"), &pos));
  pos = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("&", strlen("&"), &pos));
  pos = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("kt:", strlen("kt:"), &pos));
  pos = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("k^C", strlen("k^C"), &pos));
  pos = 0;
  // Imcomplete string
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("kth4yp", strlen("kth4yp"), &pos));
  pos = 0;
  EXPECT_EQ(0, VLQBase64Decode("A", strlen("A"), &pos));
  EXPECT_EQ(1ul, pos);
  pos = 0;
  EXPECT_EQ(1, VLQBase64Decode("C", strlen("C"), &pos));
  EXPECT_EQ(1ul, pos);
  pos = 0;
  EXPECT_EQ(12, VLQBase64Decode("Y", strlen("Y"), &pos));
  EXPECT_EQ(1ul, pos);
  pos = 0;
  EXPECT_EQ(123, VLQBase64Decode("2H", strlen("2H"), &pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(1234, VLQBase64Decode("ktC", strlen("ktC"), &pos));
  EXPECT_EQ(3ul, pos);
  pos = 0;
  EXPECT_EQ(12345, VLQBase64Decode("yjY", strlen("yjY"), &pos));
  EXPECT_EQ(3ul, pos);
  pos = 0;
  EXPECT_EQ(123456, VLQBase64Decode("gkxH", strlen("gkxH"), &pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(1234567, VLQBase64Decode("uorrC", strlen("uorrC"), &pos));
  EXPECT_EQ(5ul, pos);
  pos = 0;
  EXPECT_EQ(12345678, VLQBase64Decode("80wxX", strlen("80wxX"), &pos));
  EXPECT_EQ(5ul, pos);
  pos = 0;
  EXPECT_EQ(123456789, VLQBase64Decode("qxmvrH", strlen("qxmvrH"), &pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(1234567890, VLQBase64Decode("kth4ypC", strlen("kth4ypC"), &pos));
  EXPECT_EQ(7ul, pos);
  pos = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::max(),
            VLQBase64Decode("+/////D", strlen("+/////D"), &pos));
  EXPECT_EQ(7ul, pos);
  pos = 0;
  // An overflowed value 12345678901 (0x2DFDC1C35)
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("qjuw7/2A", strlen("qjuw7/2A"), &pos));
  pos = 0;
  // An overflowed value 123456789012(0x1CBE991A14)
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("ohtkz+lH", strlen("ohtkz+lH"), &pos));
  pos = 0;
  EXPECT_EQ(static_cast<int>(0x60000000),
            VLQBase64Decode("ggggggD", strlen("ggggggD"), &pos));
  pos = 0;
  // An overflowed value 4294967296  (0x100000000)
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("ggggggE", strlen("ggggggE"), &pos));
  pos = 0;
  EXPECT_EQ(-1, VLQBase64Decode("D", strlen("D"), &pos));
  EXPECT_EQ(1ul, pos);
  pos = 0;
  EXPECT_EQ(-12, VLQBase64Decode("Z", strlen("Z"), &pos));
  EXPECT_EQ(1ul, pos);
  pos = 0;
  EXPECT_EQ(-123, VLQBase64Decode("3H", strlen("3H"), &pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(-1234, VLQBase64Decode("ltC", strlen("ltC"), &pos));
  EXPECT_EQ(3ul, pos);
  pos = 0;
  EXPECT_EQ(-12345, VLQBase64Decode("zjY", strlen("zjY"), &pos));
  EXPECT_EQ(3ul, pos);
  pos = 0;
  EXPECT_EQ(-123456, VLQBase64Decode("hkxH", strlen("hkxH"), &pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(-1234567, VLQBase64Decode("vorrC", strlen("vorrC"), &pos));
  EXPECT_EQ(5ul, pos);
  pos = 0;
  EXPECT_EQ(-12345678, VLQBase64Decode("90wxX", strlen("90wxX"), &pos));
  EXPECT_EQ(5ul, pos);
  pos = 0;
  EXPECT_EQ(-123456789, VLQBase64Decode("rxmvrH", strlen("rxmvrH"), &pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(-1234567890, VLQBase64Decode("lth4ypC", strlen("lth4ypC"), &pos));
  EXPECT_EQ(7ul, pos);
  pos = 0;
  EXPECT_EQ(-std::numeric_limits<int32_t>::max(),
            VLQBase64Decode("//////D", strlen("//////D"), &pos));
  EXPECT_EQ(7ul, pos);
  pos = 0;
  // An overflowed value -12345678901, |value| = (0x2DFDC1C35)
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("rjuw7/2A", strlen("rjuw7/2A"), &pos));
  pos = 0;
  // An overflowed value -123456789012,|value| = (0x1CBE991A14)
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("phtkz+lH", strlen("phtkz+lH"), &pos));
  pos = 0;
  EXPECT_EQ(-static_cast<int>(0x60000000),
            VLQBase64Decode("hgggggD", strlen("hgggggD"), &pos));
  pos = 0;
  // An overflowed value -4294967296,  |value| = (0x100000000)
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            VLQBase64Decode("hgggggE", strlen("hgggggE"), &pos));
}

TEST(VLQBASE64, DecodeTwoSegment) {
  size_t pos = 0;
  EXPECT_EQ(0, VLQBase64Decode("AA", strlen("AA"), &pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("AA", strlen("AA"), &pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(5, VLQBase64Decode("KA", strlen("KA"), &pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("KA", strlen("KA"), &pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(0, VLQBase64Decode("AQ", strlen("AQ"), &pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(8, VLQBase64Decode("AQ", strlen("AQ"), &pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(6, VLQBase64Decode("MG", strlen("MG"), &pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(3, VLQBase64Decode("MG", strlen("MG"), &pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(13, VLQBase64Decode("a4E", strlen("a4E"), &pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(76, VLQBase64Decode("a4E", strlen("a4E"), &pos));
  EXPECT_EQ(3ul, pos);
  pos = 0;
  EXPECT_EQ(108, VLQBase64Decode("4GyO", strlen("4GyO"), &pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(233, VLQBase64Decode("4GyO", strlen("4GyO"), &pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(2048, VLQBase64Decode("ggEqnD", strlen("ggEqnD"), &pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(1653, VLQBase64Decode("ggEqnD", strlen("ggEqnD"), &pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(65376, VLQBase64Decode("g2/D0ilF", strlen("g2/D0ilF"), &pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(84522, VLQBase64Decode("g2/D0ilF", strlen("g2/D0ilF"), &pos));
  EXPECT_EQ(8ul, pos);
  pos = 0;
  EXPECT_EQ(537798, VLQBase64Decode("ss6gBy0m3B", strlen("ss6gBy0m3B"), &pos));
  EXPECT_EQ(5ul, pos);
  EXPECT_EQ(904521, VLQBase64Decode("ss6gBy0m3B", strlen("ss6gBy0m3B"), &pos));
  EXPECT_EQ(10ul, pos);
  pos = 0;
  EXPECT_EQ(-5, VLQBase64Decode("LA", strlen("LA"), &pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("LA", strlen("LA"), &pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(0, VLQBase64Decode("AR", strlen("AR"), &pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(-8, VLQBase64Decode("AR", strlen("AR"), &pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(-6, VLQBase64Decode("NH", strlen("NH"), &pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(-3, VLQBase64Decode("NH", strlen("NH"), &pos));
  EXPECT_EQ(2ul, pos);
  pos = 0;
  EXPECT_EQ(-13, VLQBase64Decode("b5E", strlen("b5E"), &pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(-76, VLQBase64Decode("b5E", strlen("b5E"), &pos));
  EXPECT_EQ(3ul, pos);
  pos = 0;
  EXPECT_EQ(-108, VLQBase64Decode("5GzO", strlen("5GzO"), &pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(-233, VLQBase64Decode("5GzO", strlen("5GzO"), &pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(-2048, VLQBase64Decode("hgErnD", strlen("hgErnD"), &pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(-1653, VLQBase64Decode("hgErnD", strlen("hgErnD"), &pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(-65376, VLQBase64Decode("h2/D1ilF", strlen("h2/D1ilF"), &pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(-84522, VLQBase64Decode("h2/D1ilF", strlen("h2/D1ilF"), &pos));
  EXPECT_EQ(8ul, pos);
  pos = 0;
  EXPECT_EQ(-537798, VLQBase64Decode("ts6gBz0m3B", strlen("ts6gBz0m3B"), &pos));
  EXPECT_EQ(5ul, pos);
  EXPECT_EQ(-904521, VLQBase64Decode("ts6gBz0m3B", strlen("ts6gBz0m3B"), &pos));
  EXPECT_EQ(10ul, pos);
  pos = 0;
  EXPECT_EQ(108, VLQBase64Decode("4GzO", strlen("4GzO"), &pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(-233, VLQBase64Decode("4GzO", strlen("4GzO"), &pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(2048, VLQBase64Decode("ggErnD", strlen("ggErnD"), &pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(-1653, VLQBase64Decode("ggErnD", strlen("ggErnD"), &pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(65376, VLQBase64Decode("g2/D1ilF", strlen("g2/D1ilF"), &pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(-84522, VLQBase64Decode("g2/D1ilF", strlen("g2/D1ilF"), &pos));
  EXPECT_EQ(8ul, pos);
  pos = 0;
  EXPECT_EQ(537798, VLQBase64Decode("ss6gBz0m3B", strlen("ss6gBz0m3B"), &pos));
  EXPECT_EQ(5ul, pos);
  EXPECT_EQ(-904521, VLQBase64Decode("ss6gBz0m3B", strlen("ss6gBz0m3B"), &pos));
  EXPECT_EQ(10ul, pos);
  pos = 0;
  EXPECT_EQ(-108, VLQBase64Decode("5GyO", strlen("5GyO"), &pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(233, VLQBase64Decode("5GyO", strlen("5GyO"), &pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(-2048, VLQBase64Decode("hgEqnD", strlen("hgEqnD"), &pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(1653, VLQBase64Decode("hgEqnD", strlen("hgEqnD"), &pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(-65376, VLQBase64Decode("h2/D0ilF", strlen("h2/D0ilF"), &pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(84522, VLQBase64Decode("h2/D0ilF", strlen("h2/D0ilF"), &pos));
  EXPECT_EQ(8ul, pos);
  pos = 0;
  EXPECT_EQ(-537798, VLQBase64Decode("ts6gBy0m3B", strlen("ts6gBy0m3B"), &pos));
  EXPECT_EQ(5ul, pos);
  EXPECT_EQ(904521, VLQBase64Decode("ts6gBy0m3B", strlen("ts6gBy0m3B"), &pos));
  EXPECT_EQ(10ul, pos);
}

TEST(VLQBASE64, DecodeFourSegment) {
  size_t pos = 0;
  EXPECT_EQ(0, VLQBase64Decode("AAAA", strlen("AAAA"), &pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("AAAA", strlen("AAAA"), &pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("AAAA", strlen("AAAA"), &pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("AAAA", strlen("AAAA"), &pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(8, VLQBase64Decode("QADA", strlen("QADA"), &pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("QADA", strlen("QADA"), &pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(-1, VLQBase64Decode("QADA", strlen("QADA"), &pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("QADA", strlen("QADA"), &pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(2, VLQBase64Decode("ECQY", strlen("ECQY"), &pos));
  EXPECT_EQ(1ul, pos);
  EXPECT_EQ(1, VLQBase64Decode("ECQY", strlen("ECQY"), &pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(8, VLQBase64Decode("ECQY", strlen("ECQY"), &pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(12, VLQBase64Decode("ECQY", strlen("ECQY"), &pos));
  EXPECT_EQ(4ul, pos);
  pos = 0;
  EXPECT_EQ(3200,
            VLQBase64Decode("goGguCioPk9I", strlen("goGguCioPk9I"), &pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(1248,
            VLQBase64Decode("goGguCioPk9I", strlen("goGguCioPk9I"), &pos));
  EXPECT_EQ(6ul, pos);
  EXPECT_EQ(7809,
            VLQBase64Decode("goGguCioPk9I", strlen("goGguCioPk9I"), &pos));
  EXPECT_EQ(9ul, pos);
  EXPECT_EQ(4562,
            VLQBase64Decode("goGguCioPk9I", strlen("goGguCioPk9I"), &pos));
  EXPECT_EQ(12ul, pos);
  pos = 0;
  EXPECT_EQ(1021, VLQBase64Decode("6/BACA", strlen("6/BACA"), &pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("6/BACA", strlen("6/BACA"), &pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(1, VLQBase64Decode("6/BACA", strlen("6/BACA"), &pos));
  EXPECT_EQ(5ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("6/BACA", strlen("6/BACA"), &pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(1207, VLQBase64Decode("urCAQA", strlen("urCAQA"), &pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("urCAQA", strlen("urCAQA"), &pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(8, VLQBase64Decode("urCAQA", strlen("urCAQA"), &pos));
  EXPECT_EQ(5ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("urCAQA", strlen("urCAQA"), &pos));
  EXPECT_EQ(6ul, pos);
  pos = 0;
  EXPECT_EQ(54, VLQBase64Decode("sDACA", strlen("sDACA"), &pos));
  EXPECT_EQ(2ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("sDACA", strlen("sDACA"), &pos));
  EXPECT_EQ(3ul, pos);
  EXPECT_EQ(1, VLQBase64Decode("sDACA", strlen("sDACA"), &pos));
  EXPECT_EQ(4ul, pos);
  EXPECT_EQ(0, VLQBase64Decode("sDACA", strlen("sDACA"), &pos));
  EXPECT_EQ(5ul, pos);
}
}  // namespace base
}  // namespace v8
