// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/init/v8.h"

#include "src/numbers/conversions.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace interpreter {

class ConversionsTest : public ::testing::Test {
 public:
  ConversionsTest() = default;
  ~ConversionsTest() override = default;

  SourcePosition toPos(int offset) {
    return SourcePosition(offset, offset % 10 - 1);
  }
};

// Some random offsets, mostly at 'suspicious' bit boundaries.

struct IntStringPair {
  int integer;
  std::string string;
};

static IntStringPair int_pairs[] = {{0, "0"},
                                    {101, "101"},
                                    {-1, "-1"},
                                    {1024, "1024"},
                                    {200000, "200000"},
                                    {-1024, "-1024"},
                                    {-200000, "-200000"},
                                    {kMinInt, "-2147483648"},
                                    {kMaxInt, "2147483647"}};

TEST_F(ConversionsTest, IntToCString) {
  std::unique_ptr<char[]> buf(new char[4096]);

  for (size_t i = 0; i < arraysize(int_pairs); i++) {
    ASSERT_STREQ(IntToCString(int_pairs[i].integer, {buf.get(), 4096}),
                 int_pairs[i].string.c_str());
  }
}

struct DoubleStringPair {
  double number;
  std::string string;
};

static DoubleStringPair double_pairs[] = {
    {0.0, "0"},
    {kMinInt, "-2147483648"},
    {kMaxInt, "2147483647"},
    // ES section 7.1.12.1 #sec-tostring-applied-to-the-number-type:
    // -0.0 is stringified to "0".
    {-0.0, "0"},
    {1.1, "1.1"},
    {0.1, "0.1"}};

TEST_F(ConversionsTest, DoubleToCString) {
  std::unique_ptr<char[]> buf(new char[4096]);

  for (size_t i = 0; i < arraysize(double_pairs); i++) {
    ASSERT_STREQ(DoubleToCString(double_pairs[i].number, {buf.get(), 4096}),
                 double_pairs[i].string.c_str());
  }
}

// DoubleToRadixCString

struct DoubleToRadixCStringTriplet {
  double value;
  int radix;
  std::string string;
};

const double min_subnormal_double = 4.9406564584124654e-324;
const double max_subnormal_double = 2.2250738585072009e-308;
const double min_normal_double = 2.2250738585072014e-308;
const double max_normal_double = 1.7976931348623157e+308;

static DoubleToRadixCStringTriplet triplets[] = {
    {min_subnormal_double, 2, "0." + std::string(1073, '0') + "1"},
    {min_subnormal_double, 16, "0." + std::string(268, '0') + "4"},
    {max_subnormal_double, 2,
     "0." + std::string(1022, '0') +
         "1111111111111111111111111111111111111111111111111111"},
    {max_subnormal_double, 16, "0." + std::string(255, '0') + "3ffffffffffffc"},
    {min_normal_double, 2, "0." + std::string(1021, '0') + "1"},
    {min_normal_double, 16, "0." + std::string(255, '0') + "4"},
    {max_normal_double, 2,
     "11111111111111111111111111111111111111111111111111111" +
         std::string(971, '0')},
    {max_normal_double, 16, "fffffffffffff8" + std::string(242, '0')},
    {5.876736982583413e-308, 2,
     "0." + std::string(1020, '0') +
         "10101001000010000111101001000100101001001001101101001"},
    // random subnormals
    {4.366643095814674e-308, 2,
     "0." + std::string(1021, '0') +
         "11111011001100100100000010010111101011111001000101111"},
    {1.49239903582234e-309, 2,
     "0." + std::string(1025, '0') +
         "1000100101011100111111000101101101011011000000011"},
    {4.3979823704274e-310, 2,
     "0." + std::string(1027, '0') +
         "10100001111010110110000001111111100000111110001"},
    {3.63456233544e-311, 2,
     "0." + std::string(1031, '0') +
         "1101011000011001101110000100010101000010101"},
    {9.30999108733e-312, 2,
     "0." + std::string(1033, '0') +
         "11011011010111100110011000011000111001011"},
    {4.4920580483e-313, 2,
     "0." + std::string(1037, '0') + "1010100101011010001010010110101011"},
    {1.556046456e-314, 2,
     "0." + std::string(1042, '0') + "10111011101110010010010101000001"},
    {2.34510004e-315, 2,
     "0." + std::string(1045, '0') + "111000100101010100011010111"},
    {7.0132612e-316, 2,
     "0." + std::string(1046, '0') + "1000011101011111110000100011"},
    {9.732888e-317, 2,
     "0." + std::string(1049, '0') + "1001011001001011110000001"},
    {3.876923e-318, 2, "0." + std::string(1054, '0') + "1011111110010011101"},
    {2.8595e-319, 2, "0." + std::string(1058, '0') + "1110001000010101"},
    {9.484e-320, 2, "0." + std::string(1059, '0') + "1001010111111"},
    {3.967e-321, 2, "0." + std::string(1064, '0') + "1100100011"},
    {1.33e-322, 2, "0." + std::string(1069, '0') + "11011"},
    {7.4e-323, 2, "0." + std::string(1070, '0') + "1111"}};

TEST_F(ConversionsTest, DoubleToRadixCString) {
  for (size_t i = 0; i < arraysize(triplets); i++) {
    auto t = triplets[i];
    char* radixCString = DoubleToRadixCString(t.value, t.radix);
    ASSERT_STREQ(radixCString, t.string.c_str());
    delete[] radixCString;
  }
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
