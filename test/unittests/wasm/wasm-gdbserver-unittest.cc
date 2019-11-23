// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

#include <string>

#include "src/wasm/gdb-server/packet.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

class WasmGdbRemoteTest : public ::testing::Test {};

TEST_F(WasmGdbRemoteTest, GdbRemotePacketAddChars) {
  Packet packet;

  // Read empty packet
  bool end_of_packet = packet.EndOfPacket();
  EXPECT_TRUE(end_of_packet);

  // Add raw chars
  packet.AddRawChar('4');
  packet.AddRawChar('2');

  std::string str;
  packet.GetString(&str);
  EXPECT_EQ("42", str);
}

TEST_F(WasmGdbRemoteTest, GdbRemotePacketAddBlock) {
  static const uint8_t block[] = {0x01, 0x02, 0x03, 0x04, 0x05,
                                  0x06, 0x07, 0x08, 0x09};
  static const size_t kLen = sizeof(block) / sizeof(uint8_t);
  Packet packet;
  packet.AddBlock(block, kLen);

  uint8_t buffer[kLen];
  bool ok = packet.GetBlock(buffer, kLen);
  EXPECT_TRUE(ok);
  EXPECT_EQ(0, memcmp(block, buffer, kLen));

  packet.Rewind();
  std::string str;
  ok = packet.GetString(&str);
  EXPECT_TRUE(ok);
  EXPECT_EQ("010203040506070809", str);
}

TEST_F(WasmGdbRemoteTest, GdbRemotePacketAddString) {
  Packet packet;
  packet.AddHexString("foobar");

  std::string str;
  bool ok = packet.GetString(&str);
  EXPECT_TRUE(ok);
  EXPECT_EQ("666f6f626172", str);

  packet.Clear();
  packet.AddHexString("GDB");
  ok = packet.GetString(&str);
  EXPECT_TRUE(ok);
  EXPECT_EQ("474442", str);
}

TEST_F(WasmGdbRemoteTest, GdbRemotePacketAddNumbers) {
  Packet packet;

  static const uint64_t u64_val = 0xdeadbeef89abcdef;
  static const uint8_t u8_val = 0x42;
  packet.AddNumberSep(u64_val, ';');
  packet.AddWord8(u8_val);

  std::string str;
  packet.GetString(&str);
  EXPECT_EQ("deadbeef89abcdef;42", str);

  packet.Rewind();
  uint64_t val = 0;
  char sep = '\0';
  bool ok = packet.GetNumberSep(&val, &sep);
  EXPECT_TRUE(ok);
  EXPECT_EQ(u64_val, val);
  uint8_t b = 0;
  ok = packet.GetWord8(&b);
  EXPECT_TRUE(ok);
  EXPECT_EQ(u8_val, b);
}

TEST_F(WasmGdbRemoteTest, GdbRemotePacketSequenceNumber) {
  Packet packet_with_sequence_num;
  packet_with_sequence_num.AddWord8(42);
  packet_with_sequence_num.AddRawChar(':');
  packet_with_sequence_num.AddHexString("foobar");

  int32_t sequence_num = 0;
  packet_with_sequence_num.ParseSequence();
  bool ok = packet_with_sequence_num.GetSequence(&sequence_num);
  EXPECT_TRUE(ok);
  EXPECT_EQ(42, sequence_num);

  Packet packet_without_sequence_num;
  packet_without_sequence_num.AddHexString("foobar");

  packet_without_sequence_num.ParseSequence();
  ok = packet_without_sequence_num.GetSequence(&sequence_num);
  EXPECT_FALSE(ok);
}

TEST_F(WasmGdbRemoteTest, GdbRemotePacketRunLengthEncoded) {
  Packet packet;
  packet.AddRawChar('0');
  packet.AddRawChar('*');
  packet.AddRawChar(' ');

  std::string str;
  bool ok = packet.GetHexString(&str);
  EXPECT_TRUE(ok);
  EXPECT_EQ("0000", std::string(packet.GetPayload()));
}

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
