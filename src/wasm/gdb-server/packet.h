// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_PACKET_H_
#define V8_INSPECTOR_GDB_SERVER_PACKET_H_

#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

#include <string>
#include <vector>

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

class Packet {
 public:
  Packet();

  // Empty the vector and reset the read/write pointers.
  void Clear();

  // Reset the read pointer, allowing the packet to be re-read.
  void Rewind();

  // Return true of the read pointer has reached the write pointer.
  bool EndOfPacket() const;

  // Store a single raw 8 bit value
  void AddRawChar(char ch);

  // Store a block of data as hex pairs per byte
  void AddBlock(const void* ptr, uint32_t len);

  // Store an 8, 16, 32, or 64 bit word as a block without removing preceeding
  // zeros.  This is used for fixed sized fields.
  void AddWord8(uint8_t val);
  void AddWord16(uint16_t val);
  void AddWord32(uint32_t val);
  void AddWord64(uint64_t val);

  // Store a number up to 64 bits, with preceeding zeros removed.  Since
  // zeros can be removed, the width of this number is unknown, and always
  // followed by NUL or a separator (non hex digit).
  void AddNumberSep(uint64_t val, char sep);

  // Add a raw string.  This is dangerous since the other side may incorrectly
  // interpret certain special characters such as: ":,#$"
  void AddString(const char* str);

  // Add escaped data according to the GDB protocol for binary data.
  void AddEscapedData(const char* data, size_t length);

  // Add a string stored as a stream of ASCII hex digit pairs.  It is safe
  // to use any character in this stream.  If this does not terminate the
  // packet, there should be a sperator (non hex digit) immediately following.
  void AddHexString(const char* str);

  // Retrieve a single character if available
  bool GetRawChar(char* ch);

  // Retrieve "len" ASCII character pairs.
  bool GetBlock(void* ptr, uint32_t len);

  // Retrieve a 8, 16, 32, or 64 bit word as pairs of hex digits.  These
  // functions will always consume bits/4 characters from the stream.
  bool GetWord8(uint8_t* val);
  bool GetWord16(uint16_t* val);
  bool GetWord32(uint32_t* val);
  bool GetWord64(uint64_t* val);

  // Retrieve a number and the separator.  If SEP is null, the separator is
  // consumed but thrown away.
  bool GetNumberSep(uint64_t* val, char* sep);

  // Get a string from the stream
  bool GetString(std::string* str);
  bool GetHexString(std::string* str);
  bool GetStringSep(std::string* str, char sep);

  // Return a pointer to the entire packet payload
  const char* GetPayload() const;
  size_t GetPayloadSize() const;

  // Returns true and the sequence number, or false if it is unset.
  bool GetSequence(int32_t* seq) const;

  // Parses sequence number in package data and moves read pointer past it.
  void ParseSequence();

  // Set the sequence number.
  void SetSequence(int32_t seq);

 private:
  int32_t seq_;
  std::vector<char> data_;
  size_t read_index_;
  size_t write_index_;
};

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
#endif  // V8_INSPECTOR_GDB_SERVER_PACKET_H_
