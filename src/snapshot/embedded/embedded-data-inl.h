// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_EMBEDDED_EMBEDDED_DATA_INL_H_
#define V8_SNAPSHOT_EMBEDDED_EMBEDDED_DATA_INL_H_

#include "src/codegen/cpu-features.h"
#include "src/snapshot/embedded/embedded-data.h"

namespace v8 {
namespace internal {

Address EmbeddedData::InstructionStartOf(Builtin builtin) const {
  // TODO(Wenqin): for some test like out/build/cctest
  // test-ptr-compr-cage/SharedPtrComprCageRace --random-seed=-1206385236
  // --nohard-abort --testing-d8-test-runner --force-slow-path in tsan bot.
  base::MutexGuard lock_guard(mutex_.Pointer());
  DCHECK(Builtins::IsBuiltinId(builtin));
  const struct LayoutDescription& desc = LayoutDescription(builtin);
  const uint8_t* result = RawCode() + desc.instruction_offset;
  DCHECK_LT(result, code_ + code_size_);
  return reinterpret_cast<Address>(result);
}

Address EmbeddedData::InstructionStartOfISX(size_t isx_idx) const {
  base::MutexGuard lock_guard(mutex_.Pointer());
  const struct LayoutDescription& desc = LayoutDescriptionForISX(isx_idx);
  const uint8_t* result = RawCode() + desc.instruction_offset;
  DCHECK_LT(result, code_ + code_size_);
  return reinterpret_cast<Address>(result);
}

Address EmbeddedData::InstructionEndOf(Builtin builtin) const {
  base::MutexGuard lock_guard(mutex_.Pointer());
  DCHECK(Builtins::IsBuiltinId(builtin));
  const struct LayoutDescription& desc = LayoutDescription(builtin);
  const uint8_t* result =
      RawCode() + desc.instruction_offset + desc.instruction_length;
  DCHECK_LT(result, code_ + code_size_);
  return reinterpret_cast<Address>(result);
}

uint32_t EmbeddedData::InstructionSizeOf(Builtin builtin) const {
  base::MutexGuard lock_guard(mutex_.Pointer());
  DCHECK(Builtins::IsBuiltinId(builtin));
  const struct LayoutDescription& desc = LayoutDescription(builtin);
  return desc.instruction_length;
}

uint32_t EmbeddedData::InstructionSizeOfISX(size_t isx_idx) const {
  base::MutexGuard lock_guard(mutex_.Pointer());
  const struct LayoutDescription& desc = LayoutDescriptionForISX(isx_idx);
  return desc.instruction_length;
}

Address EmbeddedData::MetadataStartOf(Builtin builtin) const {
  base::MutexGuard lock_guard(mutex_.Pointer());
  DCHECK(Builtins::IsBuiltinId(builtin));
  const struct LayoutDescription& desc = LayoutDescription(builtin);
  const uint8_t* result = RawMetadata() + desc.metadata_offset;
  DCHECK_LE(desc.metadata_offset, data_size_);
  return reinterpret_cast<Address>(result);
}

Address EmbeddedData::MetadataStartOfISX(size_t isx_idx) const {
  base::MutexGuard lock_guard(mutex_.Pointer());
  const struct LayoutDescription& desc = LayoutDescriptionForISX(isx_idx);
  const uint8_t* result = RawMetadata() + desc.metadata_offset;
  DCHECK_LE(desc.metadata_offset, data_size_);
  return reinterpret_cast<Address>(result);
}

Address EmbeddedData::InstructionStartOfBytecodeHandlers() const {
  return InstructionStartOf(Builtin::kFirstBytecodeHandler);
}

Address EmbeddedData::InstructionEndOfBytecodeHandlers() const {
  static_assert(Builtins::kBytecodeHandlersAreSortedLast);
  // Note this also includes trailing padding, but that's fine for our purposes.
  return reinterpret_cast<Address>(code_ + code_size_);
}

uint32_t EmbeddedData::PaddedInstructionSizeOf(Builtin builtin) const {
  uint32_t size = InstructionSizeOf(builtin);
  CHECK_NE(size, 0);
  return PadAndAlignCode(size);
}

uint32_t EmbeddedData::PaddedInstructionSizeOfISX(size_t isx_idx) const {
  uint32_t size = InstructionSizeOfISX(isx_idx);
  CHECK_NE(size, 0);
  return PadAndAlignCode(size);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_EMBEDDED_EMBEDDED_DATA_INL_H_
