// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_GDB_SERVER_UTIL_H_
#define V8_INSPECTOR_GDB_SERVER_UTIL_H_

#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

#include <string>
#include <vector>
#include "src/flags/flags.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

#define TRACE_GDB_REMOTE(...)                                            \
  do {                                                                   \
    if (FLAG_trace_wasm_gdb_remote) PrintF("[gdb-remote] " __VA_ARGS__); \
  } while (false)

// Convert from ASCII (0-9,a-f,A-F) to 4b unsigned or return
// false if the input char is unexpected.
bool NibbleToInt(char inChar, int* outInt);

// Convert from 0-15 to ASCII (0-9,a-f) or return false
// if the input is not a value from 0-15.
bool IntToNibble(int inInt, char* outChar);

// Convert a pair of nibbles to a value from 0-255 or return
// false if ethier input character is not a valid nibble.
bool NibblesToByte(const char* inStr, int* outInt);

std::vector<std::string> StringSplit(const std::string& instr,
                                     const char* delim);

// Convert the memory pointed to by mem into hex.
std::string Mem2Hex(const uint8_t* mem, size_t count);
std::string Mem2Hex(const char* str);

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
#endif  // V8_INSPECTOR_GDB_SERVER_UTIL_H_
