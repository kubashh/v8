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

template <size_t ResultSize, size_t PosSize>
void TestVLQBase64Decode(const char* str,
                         const int32_t (&expect_results)[ResultSize],
                         const size_t (&expect_posses)[PosSize]) {
  EXPECT_EQ(ResultSize, PosSize);
  size_t pos = 0;
  for (size_t i = 0; i < ResultSize; ++i) {
    int32_t result = VLQBase64Decode(str, strlen(str), &pos);
    EXPECT_EQ(expect_results[i], result);
    EXPECT_EQ(expect_posses[i], pos);
  }
}

TEST(VLQBASE64, DecodeOneSegment) {
  int32_t expected_res1[] = {std::numeric_limits<int32_t>::min()};
  size_t expected_pos1[] = {0};
  TestVLQBase64Decode("", expected_res1, expected_pos1);

  // Strings with unsupported symbol.
  int32_t expected_res2[] = {std::numeric_limits<int32_t>::min()};
  size_t expected_pos2[] = {0};
  TestVLQBase64Decode("*", expected_res2, expected_pos2);

  int32_t expected_res3[] = {std::numeric_limits<int32_t>::min()};
  size_t expected_pos3[] = {0};
  TestVLQBase64Decode("&", expected_res3, expected_pos3);

  int32_t expected_res4[] = {std::numeric_limits<int32_t>::min()};
  size_t expected_pos4[] = {2};
  TestVLQBase64Decode("kt:", expected_res4, expected_pos4);

  int32_t expected_res5[] = {std::numeric_limits<int32_t>::min()};
  size_t expected_pos5[] = {1};
  TestVLQBase64Decode("k^C", expected_res5, expected_pos5);

  // Imcomplete string.
  int32_t expected_res6[] = {std::numeric_limits<int32_t>::min()};
  size_t expected_pos6[] = {6};
  TestVLQBase64Decode("kth4yp", expected_res6, expected_pos6);

  // Interpretable strings.
  int32_t expected_res7[] = {0};
  size_t expected_pos7[] = {1};
  TestVLQBase64Decode("A", expected_res7, expected_pos7);

  int32_t expected_res8[] = {1};
  size_t expected_pos8[] = {1};
  TestVLQBase64Decode("C", expected_res8, expected_pos8);

  int32_t expected_res9[] = {12};
  size_t expected_pos9[] = {1};
  TestVLQBase64Decode("Y", expected_res9, expected_pos9);

  int32_t expected_res10[] = {123};
  size_t expected_pos10[] = {2};
  TestVLQBase64Decode("2H", expected_res10, expected_pos10);

  int32_t expected_res11[] = {1234};
  size_t expected_pos11[] = {3};
  TestVLQBase64Decode("ktC", expected_res11, expected_pos11);

  int32_t expected_res12[] = {12345};
  size_t expected_pos12[] = {3};
  TestVLQBase64Decode("yjY", expected_res12, expected_pos12);

  int32_t expected_res13[] = {123456};
  size_t expected_pos13[] = {4};
  TestVLQBase64Decode("gkxH", expected_res13, expected_pos13);

  int32_t expected_res14[] = {1234567};
  size_t expected_pos14[] = {5};
  TestVLQBase64Decode("uorrC", expected_res14, expected_pos14);

  int32_t expected_res15[] = {12345678};
  size_t expected_pos15[] = {5};
  TestVLQBase64Decode("80wxX", expected_res15, expected_pos15);

  int32_t expected_res16[] = {123456789};
  size_t expected_pos16[] = {6};
  TestVLQBase64Decode("qxmvrH", expected_res16, expected_pos16);

  int32_t expected_res17[] = {1234567890};
  size_t expected_pos17[] = {7};
  TestVLQBase64Decode("kth4ypC", expected_res17, expected_pos17);

  int32_t expected_res18[] = {std::numeric_limits<int32_t>::max()};
  size_t expected_pos18[] = {7};
  TestVLQBase64Decode("+/////D", expected_res18, expected_pos18);

  int32_t expected_res19[] = {-1};
  size_t expected_pos19[] = {1};
  TestVLQBase64Decode("D", expected_res19, expected_pos19);

  int32_t expected_res20[] = {-12};
  size_t expected_pos20[] = {1};
  TestVLQBase64Decode("Z", expected_res20, expected_pos20);

  int32_t expected_res21[] = {-123};
  size_t expected_pos21[] = {2};
  TestVLQBase64Decode("3H", expected_res21, expected_pos21);

  int32_t expected_res22[] = {-1234};
  size_t expected_pos22[] = {3};
  TestVLQBase64Decode("ltC", expected_res22, expected_pos22);

  int32_t expected_res23[] = {-12345};
  size_t expected_pos23[] = {3};
  TestVLQBase64Decode("zjY", expected_res23, expected_pos23);

  int32_t expected_res24[] = {-123456};
  size_t expected_pos24[] = {4};
  TestVLQBase64Decode("hkxH", expected_res24, expected_pos24);

  int32_t expected_res25[] = {-1234567};
  size_t expected_pos25[] = {5};
  TestVLQBase64Decode("vorrC", expected_res25, expected_pos25);

  int32_t expected_res26[] = {-12345678};
  size_t expected_pos26[] = {5};
  TestVLQBase64Decode("90wxX", expected_res26, expected_pos26);

  int32_t expected_res27[] = {-123456789};
  size_t expected_pos27[] = {6};
  TestVLQBase64Decode("rxmvrH", expected_res27, expected_pos27);

  int32_t expected_res28[] = {-1234567890};
  size_t expected_pos28[] = {7};
  TestVLQBase64Decode("lth4ypC", expected_res28, expected_pos28);

  int32_t expected_res29[] = {-std::numeric_limits<int32_t>::max()};
  size_t expected_pos29[] = {7};
  TestVLQBase64Decode("//////D", expected_res29, expected_pos29);

  // An overflowed value 12345678901 (0x2DFDC1C35).
  int32_t expected_res30[] = {std::numeric_limits<int32_t>::min()};
  size_t expected_pos30[] = {6};
  TestVLQBase64Decode("qjuw7/2A", expected_res30, expected_pos30);

  // An overflowed value 123456789012(0x1CBE991A14).
  int32_t expected_res31[] = {std::numeric_limits<int32_t>::min()};
  size_t expected_pos31[] = {6};
  TestVLQBase64Decode("ohtkz+lH", expected_res31, expected_pos31);

  // An overflowed value 4294967296  (0x100000000).
  int32_t expected_res32[] = {std::numeric_limits<int32_t>::min()};
  size_t expected_pos32[] = {6};
  TestVLQBase64Decode("ggggggE", expected_res32, expected_pos32);

  // An overflowed value -12345678901, |value| = (0x2DFDC1C35).
  int32_t expected_res33[] = {std::numeric_limits<int32_t>::min()};
  size_t expected_pos33[] = {6};
  TestVLQBase64Decode("rjuw7/2A", expected_res33, expected_pos33);

  // An overflowed value -123456789012,|value| = (0x1CBE991A14).
  int32_t expected_res34[] = {std::numeric_limits<int32_t>::min()};
  size_t expected_pos34[] = {6};
  TestVLQBase64Decode("phtkz+lH", expected_res34, expected_pos34);

  // An overflowed value -4294967296,  |value| = (0x100000000).
  int32_t expected_res35[] = {std::numeric_limits<int32_t>::min()};
  size_t expected_pos35[] = {6};
  TestVLQBase64Decode("hgggggE", expected_res35, expected_pos35);
}

TEST(VLQBASE64, DecodeTwoSegment) {
  int32_t expected_res1[] = {0, 0};
  size_t expected_pos1[] = {1ul, 2ul};
  TestVLQBase64Decode("AA", expected_res1, expected_pos1);

  int32_t expected_res2[] = {5, 0};
  size_t expected_pos2[] = {1ul, 2ul};
  TestVLQBase64Decode("KA", expected_res2, expected_pos2);

  int32_t expected_res3[] = {0, 8};
  size_t expected_pos3[] = {1ul, 2ul};
  TestVLQBase64Decode("AQ", expected_res3, expected_pos3);

  int32_t expected_res4[] = {6, 3};
  size_t expected_pos4[] = {1ul, 2ul};
  TestVLQBase64Decode("MG", expected_res4, expected_pos4);

  int32_t expected_res5[] = {13, 76};
  size_t expected_pos5[] = {1ul, 3ul};
  TestVLQBase64Decode("a4E", expected_res5, expected_pos5);

  int32_t expected_res6[] = {108, 233};
  size_t expected_pos6[] = {2ul, 4ul};
  TestVLQBase64Decode("4GyO", expected_res6, expected_pos6);

  int32_t expected_res7[] = {2048, 1653};
  size_t expected_pos7[] = {3ul, 6ul};
  TestVLQBase64Decode("ggEqnD", expected_res7, expected_pos7);

  int32_t expected_res8[] = {65376, 84522};
  size_t expected_pos8[] = {4ul, 8ul};
  TestVLQBase64Decode("g2/D0ilF", expected_res8, expected_pos8);

  int32_t expected_res9[] = {537798, 904521};
  size_t expected_pos9[] = {5ul, 10ul};
  TestVLQBase64Decode("ss6gBy0m3B", expected_res9, expected_pos9);

  int32_t expected_res10[] = {-5, 0};
  size_t expected_pos10[] = {1ul, 2ul};
  TestVLQBase64Decode("LA", expected_res10, expected_pos10);

  int32_t expected_res11[] = {0, -8};
  size_t expected_pos11[] = {1ul, 2ul};
  TestVLQBase64Decode("AR", expected_res11, expected_pos11);

  int32_t expected_res12[] = {-6, -3};
  size_t expected_pos12[] = {1ul, 2ul};
  TestVLQBase64Decode("NH", expected_res12, expected_pos12);

  int32_t expected_res13[] = {-13, -76};
  size_t expected_pos13[] = {1ul, 3ul};
  TestVLQBase64Decode("b5E", expected_res13, expected_pos13);

  int32_t expected_res14[] = {-108, -233};
  size_t expected_pos14[] = {2ul, 4ul};
  TestVLQBase64Decode("5GzO", expected_res14, expected_pos14);

  int32_t expected_res15[] = {-2048, -1653};
  size_t expected_pos15[] = {3ul, 6ul};
  TestVLQBase64Decode("hgErnD", expected_res15, expected_pos15);

  int32_t expected_res16[] = {-65376, -84522};
  size_t expected_pos16[] = {4ul, 8ul};
  TestVLQBase64Decode("h2/D1ilF", expected_res16, expected_pos16);

  int32_t expected_res17[] = {-537798, -904521};
  size_t expected_pos17[] = {5ul, 10ul};
  TestVLQBase64Decode("ts6gBz0m3B", expected_res17, expected_pos17);

  int32_t expected_res18[] = {108, -233};
  size_t expected_pos18[] = {2ul, 4ul};
  TestVLQBase64Decode("4GzO", expected_res18, expected_pos18);

  int32_t expected_res19[] = {2048, -1653};
  size_t expected_pos19[] = {3ul, 6ul};
  TestVLQBase64Decode("ggErnD", expected_res19, expected_pos19);

  int32_t expected_res20[] = {65376, -84522};
  size_t expected_pos20[] = {4ul, 8ul};
  TestVLQBase64Decode("g2/D1ilF", expected_res20, expected_pos20);

  int32_t expected_res21[] = {537798, -904521};
  size_t expected_pos21[] = {5ul, 10ul};
  TestVLQBase64Decode("ss6gBz0m3B", expected_res21, expected_pos21);

  int32_t expected_res22[] = {-108, 233};
  size_t expected_pos22[] = {2ul, 4ul};
  TestVLQBase64Decode("5GyO", expected_res22, expected_pos22);

  int32_t expected_res23[] = {-2048, 1653};
  size_t expected_pos23[] = {3ul, 6ul};
  TestVLQBase64Decode("hgEqnD", expected_res23, expected_pos23);

  int32_t expected_res24[] = {-65376, 84522};
  size_t expected_pos24[] = {4ul, 8ul};
  TestVLQBase64Decode("h2/D0ilF", expected_res24, expected_pos24);

  int32_t expected_res25[] = {-537798, 904521};
  size_t expected_pos25[] = {5ul, 10ul};
  TestVLQBase64Decode("ts6gBy0m3B", expected_res25, expected_pos25);
}

TEST(VLQBASE64, DecodeFourSegment) {
  int32_t expected_res1[] = {0, 0, 0, 0};
  size_t expected_pos1[] = {1ul, 2ul, 3ul, 4ul};
  TestVLQBase64Decode("AAAA", expected_res1, expected_pos1);

  int32_t expected_res2[] = {8, 0, -1, 0};
  size_t expected_pos2[] = {1ul, 2ul, 3ul, 4ul};
  TestVLQBase64Decode("QADA", expected_res2, expected_pos2);

  int32_t expected_res3[] = {2, 1, 8, 12};
  size_t expected_pos3[] = {1ul, 2ul, 3ul, 4ul};
  TestVLQBase64Decode("ECQY", expected_res3, expected_pos3);

  int32_t expected_res4[] = {3200, 1248, 7809, 4562};
  size_t expected_pos4[] = {3ul, 6ul, 9ul, 12ul};
  TestVLQBase64Decode("goGguCioPk9I", expected_res4, expected_pos4);

  int32_t expected_res5[] = {1021, 0, 1, 0};
  size_t expected_pos5[] = {3ul, 4ul, 5ul, 6ul};
  TestVLQBase64Decode("6/BACA", expected_res5, expected_pos5);

  int32_t expected_res6[] = {1207, 0, 8, 0};
  size_t expected_pos6[] = {3ul, 4ul, 5ul, 6ul};
  TestVLQBase64Decode("urCAQA", expected_res6, expected_pos6);

  int32_t expected_res7[] = {54, 0, 1, 0};
  size_t expected_pos7[] = {2ul, 3ul, 4ul, 5ul};
  TestVLQBase64Decode("sDACA", expected_res7, expected_pos7);
}
}  // namespace base
}  // namespace v8
