// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/builtins-jump-table-info.h"

#include "src/base/memory.h"
#include "src/codegen/assembler-inl.h"
#include "src/common/globals.h"
#include "v8-internal.h"

namespace v8 {
namespace internal {

namespace {
static constexpr uint8_t kOffsetToFirstEntry = kUInt32Size;
static constexpr uint8_t kOffsetToPCOffset = 0;
static constexpr uint8_t kOffsetToValue =
    sizeof(BuiltinJumpTableInfoEntry::pc_offset);
static constexpr uint8_t kEntrySize =
    sizeof(BuiltinJumpTableInfoEntry::pc_offset) +
    sizeof(BuiltinJumpTableInfoEntry::target);
}  // namespace

// static
constexpr uint32_t BuiltinJumpTableInfoEntry::size() { return kEntrySize; }

void BuiltinJumpTableInfoWriter::Add(uint32_t pc_offset, int32_t target) {
  BuiltinJumpTableInfoEntry entry = {pc_offset, target};
  byte_count_ += BuiltinJumpTableInfoEntry::size();
  entries_.push_back(entry);
}

size_t BuiltinJumpTableInfoWriter::entry_count() const {
  return entries_.size();
}

uint32_t BuiltinJumpTableInfoWriter::section_size() const {
  return kOffsetToFirstEntry + byte_count_;
}

void BuiltinJumpTableInfoWriter::Emit(Assembler* assm) {
  assm->dd(section_size());
  for (auto i = entries_.begin(); i != entries_.end(); ++i) {
    assm->dd(i->pc_offset);
    assm->dd(i->target);
  }
}

BuiltinJumpTableInfoIterator::BuiltinJumpTableInfoIterator(Address start,
                                                           uint32_t size)
    : builtin_jump_table_info_start_(start),
      builtin_jump_table_info_size_(size),
      current_entry_(builtin_jump_table_info_start_ + kOffsetToFirstEntry) {
  DCHECK_NE(kNullAddress, start);
  DCHECK_IMPLIES(size, size == base::ReadUnalignedValue<uint32_t>(
                                   builtin_jump_table_info_start_));
}

uint32_t BuiltinJumpTableInfoIterator::GetPCOffset() const {
  return base::ReadUnalignedValue<uint32_t>(current_entry_ + kOffsetToPCOffset);
}

int32_t BuiltinJumpTableInfoIterator::GetTarget() const {
  return base::ReadUnalignedValue<int32_t>(current_entry_ + kOffsetToValue);
}

void BuiltinJumpTableInfoIterator::Next() {
  current_entry_ += BuiltinJumpTableInfoEntry::size();
}

bool BuiltinJumpTableInfoIterator::HasCurrent() const {
  return current_entry_ <
         builtin_jump_table_info_start_ + builtin_jump_table_info_size_;
}

}  // namespace internal
}  // namespace v8
