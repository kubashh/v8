// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/constant-pool.h"
#include "src/assembler-arch.h"

namespace v8 {
namespace internal {

// Constant Pool.

ConstantPool::ConstantPool(Assembler* assm) : assm_(assm) {}
ConstantPool::~ConstantPool() { DCHECK_EQ(blocked_nesting_, 0); }

bool ConstantPool::RecordEntry(uint32_t data, RelocInfo::Mode rmode) {
  ConstantPoolKey key(data, rmode);
  CHECK(key.is_value32());
  return RecordKey(std::move(key), assm_->pc_offset());
}

bool ConstantPool::RecordEntry(uint64_t data, RelocInfo::Mode rmode) {
  ConstantPoolKey key(data, rmode);
  CHECK(!key.is_value32());
  return RecordKey(std::move(key), assm_->pc_offset());
}

bool ConstantPool::RecordKey(ConstantPoolKey key, int offset) {
  bool write_reloc_info = !IsDuplicate(key);
  if (write_reloc_info) {
    if (key.is_value32()) {
      if (entry32_count_ == 0) first_use_32_ = offset;
      ++entry32_count_;
    } else {
      if (entry64_count_ == 0) first_use_64_ = offset;
      ++entry64_count_;
    }
  }
  entries_.insert(std::make_pair(key, offset));

  if (Entry32Count() + Entry64Count() >
      ConstantPool::kApproxMaxPoolEntryCount) {
    // Request constant pool emission after the next instruction.
    SetNextCheckIn(1);
  }

  return write_reloc_info;
}

bool ConstantPool::IsDuplicate(const ConstantPoolKey& key) {
  if (!key.AllowsDeduplication()) return false;
  auto existing = entries_.find(key);
  return existing != entries_.end();
}

void ConstantPool::Emit(bool require_jump) {
  DCHECK(!IsBlocked());
  // Prevent recursive pool emission and protect from veneer pools.
  BlockScope block(this);
  bool require_alignment =
      IsAlignmentRequiredIfEmittedAt(require_jump, assm_->pc_offset());
  int size = ComputeSize(require_jump, require_alignment);
  Label size_check;
  assm_->bind(&size_check);
  assm_->RecordConstPool(size);

  // Emit the constant pool. It is preceded by an optional branch if
  // {require_jump} and a header which will:
  //  1) Encode the size of the constant pool, for use by the disassembler.
  //  2) Terminate the program, to try to prevent execution from accidentally
  //     flowing into the constant pool.
  //  3) align the 64bit pool entries to 64-bit.
  // TODO(all): Make the alignment part less fragile. Currently code is
  // allocated as a byte array so there are no guarantees the alignment will
  // be preserved on compaction. Currently it works as allocation seems to be
  // 64-bit aligned.

  Label after_pool;
  if (require_jump) assm_->b(&after_pool);

  assm_->RecordComment("[ Constant Pool");
  EmitMarker(require_alignment);
  EmitGuard();
  if (require_alignment) assm_->Align(8);
  EmitEntries();
  assm_->RecordComment("]");

  if (after_pool.is_linked()) assm_->bind(&after_pool);

  DCHECK_EQ(assm_->SizeOfCodeGeneratedSince(&size_check), size);
  Clear();
}

void ConstantPool::Clear() {
  entries_.clear();
  first_use_32_ = -1;
  first_use_64_ = -1;
  entry32_count_ = 0;
  entry64_count_ = 0;
  next_check_ = 0;
  blocked_until_ = 0;
}

void ConstantPool::StartBlock() {
  if (blocked_nesting_++ == 0) {
    // Prevent constant pool checks happening by setting the next check to
    // the biggest possible offset.
    next_check_ = kMaxInt;
  }
}

void ConstantPool::EndBlock() {
  if (--blocked_nesting_ == 0) {
    DCHECK(IsInImmRangeIfEmittedAt(assm_->pc_offset()));
    // Two cases:
    //  * blocked_until_ >= next_check_ and the emission is still blocked
    //  * blocked_until_ < next_check_ and the next emit will trigger a check.
    next_check_ = blocked_until_;
  }
}

bool ConstantPool::IsBlocked() const {
  return (blocked_nesting_ > 0) || (assm_->pc_offset() < blocked_until_);
}

void ConstantPool::SetNextCheckIn(size_t instructions) {
  next_check_ =
      assm_->pc_offset() + static_cast<int>(instructions * kInstrSize);
}

void ConstantPool::BlockFor(int instructions) {
  int pc_limit = assm_->pc_offset() + instructions * kInstrSize;
  if (blocked_until_ < pc_limit) {
    blocked_until_ = pc_limit;
    DCHECK(IsInImmRangeIfEmittedAt(pc_limit));
  }
  if (next_check_ < blocked_until_) {
    next_check_ = blocked_until_;
  }
}

void ConstantPool::EmitEntries() {
  for (auto iter = entries_.begin(); iter != entries_.end();) {
    DCHECK(iter->first.is_value32() || IsAligned(assm_->pc_offset(), 8));
    auto range = entries_.equal_range(iter->first);
    bool shared = iter->first.AllowsDeduplication();
    for (auto it = range.first; it != range.second; ++it) {
      SetLoadOffsetToConstPoolEntry(it->second, assm_->pc_offset(), it->first);
      if (!shared) Emit(it->first);
    }
    if (shared) Emit(iter->first);
    iter = range.second;
  }
}

void ConstantPool::Emit(const ConstantPoolKey& key) {
  if (key.is_value32()) {
    assm_->dd(key.value32());
  } else {
    assm_->dq(key.value64());
  }
}

bool ConstantPool::ShouldEmitNow(bool require_jump) const {
  if (IsEmpty()) return false;
  // We compute {dist32/64}, i.e. the distance from the first instruction
  // accessing a 32bit/64bit entry in the constant pool to any of the
  // 32bit/64bit constant pool entries, respectively. The constant pool should
  // be emitted if either of the following is true: (A) {dist32/64} will be out
  // of range at the next check in. (B) Emission can be done behind an
  // unconditional branch and {dist32/64} exceeds {kOpportunityDist*}. (C)
  // {dist32/64} exceeds the desired approximate distance to the pool.
  bool require_alignment =
      IsAlignmentRequiredIfEmittedAt(require_jump, assm_->pc_offset());
  size_t pool_end_32 =
      assm_->pc_offset() + ComputeSize(require_jump, require_alignment);
  size_t pool_end_64 = pool_end_32 - Entry32Count() * 4;
  if (Entry64Count() != 0) {
    // The 64-bit constants are always emitted before the 32-bit constants, so
    // we subtract the size of the 32-bit constants from {size}.
    size_t dist64 = pool_end_64 - first_use_64_;
    if ((dist64 + kCheckConstPoolInterval >= kMaxDistToPool64) ||
        (!require_jump && (dist64 >= kOpportunityDistToPool64)) ||
        (dist64 >= kApproxDistToPool64)) {
      return true;
    }
  }
  if (Entry32Count() != 0) {
    size_t dist32 = pool_end_32 - first_use_32_;
    if ((dist32 + kCheckConstPoolInterval >= kMaxDistToPool32) ||
        (!require_jump && (dist32 >= kOpportunityDistToPool32)) ||
        (dist32 >= kApproxDistToPool32)) {
      return true;
    }
  }
  return false;
}

int ConstantPool::ComputeSize(bool require_jump, bool require_alignment) const {
  int size_up_to_marker = PrologueSize(require_jump);
  size_t size_after_marker = Entry32Count() * 4 +
                             (require_alignment ? kInstrSize : 0) +
                             Entry64Count() * 8;
  return size_up_to_marker + static_cast<int>(size_after_marker);
}

bool ConstantPool::IsAlignmentRequiredIfEmittedAt(bool require_jump,
                                                  int pc_offset) const {
  int size_up_to_marker = PrologueSize(require_jump);
  return Entry64Count() != 0 && !IsAligned(pc_offset + size_up_to_marker, 8);
}

bool ConstantPool::IsInImmRangeIfEmittedAt(int pc_offset) {
  // Check that all entries are in range if the pool is emitted at {pc_offset}.
  // This ignores kPcLoadDelta (conservatively, since all offsets are positive).
  bool require_alignment = IsAlignmentRequiredIfEmittedAt(true, pc_offset);
  size_t last_entry_32 = pc_offset + ComputeSize(true, require_alignment) - 4;
  size_t last_entry_64 = last_entry_32 - Entry32Count() * 4 - 8;
  bool entries_in_range_32 =
      Entry32Count() == 0 ||
      (last_entry_32 <= first_use_32_ + kMaxDistToPool32);
  bool entries_in_range_64 =
      Entry64Count() == 0 ||
      (last_entry_64 <= first_use_64_ + kMaxDistToPool64);
  return entries_in_range_32 && entries_in_range_64;
}

}  // namespace internal
}  // namespace v8
