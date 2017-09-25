// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include "src/unicode-decoder.h"
#include "src/unicode-inl.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

namespace {

using Utf8Decoder = unibrow::Utf8Decoder<512>;

void Decode(Utf8Decoder* decoder, const std::string& str) {
  // Put the string in its own buffer on the heap to make sure that
  // AddressSanitizer's heap-buffer-overflow logic can see what's going on.
  std::unique_ptr<char[]> buffer(new char[str.length()]);
  memcpy(buffer.get(), str.data(), str.length());
  decoder->Reset(buffer.get(), str.length());
}

void DecodeNormally(const std::vector<byte>& bytes,
                    std::vector<unibrow::uchar>* output) {
  size_t cursor = 0;
  while (cursor < bytes.size()) {
    output->push_back(
        unibrow::Utf8::ValueOf(bytes.data() + cursor, bytes.size(), &cursor));
  }
}

void DecodeIncrementally(const std::vector<byte>& bytes,
                         std::vector<unibrow::uchar>* output) {
  unibrow::Utf8::Utf8IncrementalBuffer buffer = 0;
  for (auto b : bytes) {
    unibrow::uchar result = unibrow::Utf8::ValueOfIncremental(b, &buffer);
    if (result != unibrow::Utf8::kIncomplete) {
      output->push_back(result);
    }
  }
  unibrow::uchar result = unibrow::Utf8::ValueOfIncrementalFinish(&buffer);
  if (result != unibrow::Utf8::kBufferEmpty) {
    output->push_back(result);
  }
}

}  // namespace

TEST(UnicodeTest, ReadOffEndOfUtf8String) {
  Utf8Decoder decoder;

  // Not enough continuation bytes before string ends.
  Decode(&decoder, "\xE0");
  Decode(&decoder, "\xED");
  Decode(&decoder, "\xF0");
  Decode(&decoder, "\xF4");
}

TEST(UnicodeTest, IncrementalUTF8DecodingVsNonIncrementalUtf8Decoding) {
  // Unfortunately, V8 has two UTF-8 decoders. This test checks that they
  // produce the same result. This test was inspired by
  // https://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt .
  typedef struct {
    std::vector<byte> bytes;
    std::vector<unibrow::uchar> unicode_expected;
  } TestCase;

  TestCase data[] = {
      // Correct UTF-8 text.
      {{0xce, 0xba, 0xe1, 0xbd, 0xb9, 0xcf, 0x83, 0xce, 0xbc, 0xce, 0xb5},
       {0x3ba, 0x1f79, 0x3c3, 0x3bc, 0x3b5}},
      // First possible sequence of a certain length:
      // 1 byte
      {{0x00}, {0x0}},
      // 2 bytes
      {{0xc2, 0x80}, {0x80}},
      // 3 bytes
      {{0xe0, 0xa0, 0x80}, {0x800}},
      // 4 bytes
      {{0xf0, 0x90, 0x80, 0x80}, {0x10000}},
      // 5 bytes (not supported)
      {{0xf8, 0x88, 0x80, 0x80, 0x80},
       {0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd}},
      // 6 bytes (not supported)
      {{0xfc, 0x84, 0x80, 0x80, 0x80, 0x80},
       {0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd}},
  };

  for (auto test : data) {
    std::vector<unibrow::uchar> output_normal;
    DecodeNormally(test.bytes, &output_normal);

    CHECK_EQ(output_normal.size(), test.unicode_expected.size());
    for (size_t i = 0; i < output_normal.size(); ++i) {
      CHECK_EQ(output_normal[i], test.unicode_expected[i]);
    }

    std::vector<unibrow::uchar> output_incremental;
    DecodeIncrementally(test.bytes, &output_incremental);

    CHECK_EQ(output_incremental.size(), test.unicode_expected.size());
    for (size_t i = 0; i < output_incremental.size(); ++i) {
      CHECK_EQ(output_incremental[i], test.unicode_expected[i]);
    }
  }
}

}  // namespace internal
}  // namespace v8
