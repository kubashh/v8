// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_X64_BUILTIN_JUMP_TABLE_INFO_H_
#define V8_CODEGEN_X64_BUILTIN_JUMP_TABLE_INFO_H_

#include <vector>

#include "include/v8-internal.h"
#include "src/base/macros.h"

namespace v8 {
namespace internal {

class Assembler;

// Builtin jump table info is a part of code metadata. It's used by disassembler
// to disasm the jump table of Table Switch in builtins.

// InstructionStream builtin jump table information section layout:
// byte count       content
// ----------------------------------------------------------------
// 4                section size as unit32_t
// [Inline array of BuiltinJumpTableInfoEntry in increasing pc_offset order]
// ┌ 4              pc_offset of entry as uint32_t
// └ 4              target of entry as int32_t

struct BuiltinJumpTableInfoEntry {
  static constexpr uint32_t size();
  uint32_t pc_offset;
  int32_t target;
};

class BuiltinJumpTableInfoWriter {
 public:
  V8_EXPORT_PRIVATE void Add(uint32_t pc_offset, int32_t target);
  void Emit(Assembler* assm);

  size_t entry_count() const;

  uint32_t section_size() const;

 private:
  uint32_t byte_count_ = 0;
  std::vector<BuiltinJumpTableInfoEntry> entries_;
};

class V8_EXPORT_PRIVATE BuiltinJumpTableInfoIterator {
 public:
  BuiltinJumpTableInfoIterator(Address start, uint32_t size);
  uint32_t GetPCOffset() const;
  int32_t GetTarget() const;
  void Next();
  bool HasCurrent() const;

 private:
  Address builtin_jump_table_info_start_;
  Address builtin_jump_table_info_size_;
  Address current_entry_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_X64_BUILTIN_JUMP_TABLE_INFO_H_
