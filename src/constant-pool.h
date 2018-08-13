// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CONSTANT_POOL_H_
#define V8_CONSTANT_POOL_H_

#include "src/assembler.h"

namespace v8 {
namespace internal {

class ConstPoolKey {
 public:
  explicit ConstPoolKey(uint64_t value, RelocInfo::Mode rmode = RelocInfo::NONE)
      : is_value32_(false), value64_(value), rmode_(rmode) {}

  explicit ConstPoolKey(uint32_t value, RelocInfo::Mode rmode = RelocInfo::NONE)
      : is_value32_(true), value32_(value), rmode_(rmode) {}

  uint64_t value64() const {
    CHECK(!is_value32_);
    return value64_;
  }
  uint32_t value32() const {
    CHECK(is_value32_);
    return value32_;
  }

  bool is_value32() const { return is_value32_; }
  RelocInfo::Mode rmode() const { return rmode_; }

  bool AllowsDeduplication() const {
    DCHECK(rmode_ != RelocInfo::COMMENT && rmode_ != RelocInfo::CONST_POOL &&
           rmode_ != RelocInfo::VENEER_POOL &&
           rmode_ != RelocInfo::DEOPT_SCRIPT_OFFSET &&
           rmode_ != RelocInfo::DEOPT_INLINING_ID &&
           rmode_ != RelocInfo::DEOPT_REASON && rmode_ != RelocInfo::DEOPT_ID);
    return RelocInfo::IsShareableRelocMode(rmode_) ||
           rmode_ == RelocInfo::CODE_TARGET;
  }

 private:
  bool is_value32_;
  union {
    uint64_t value64_;
    uint32_t value32_;
  };
  RelocInfo::Mode rmode_;
};

// Order for pool entries. 64bit entries go first.
inline bool operator<(const ConstPoolKey& a, const ConstPoolKey& b) {
  if (a.is_value32() < b.is_value32()) return true;
  if (a.is_value32() > b.is_value32()) return false;
  if (a.rmode() < b.rmode()) return true;
  if (a.rmode() > b.rmode()) return false;
  return a.is_value32() ? a.value32() < b.value32() : a.value64() < b.value64();
}

inline bool operator==(const ConstPoolKey& a, const ConstPoolKey& b) {
  if (a.rmode() != b.rmode() || a.is_value32() != b.is_value32()) {
    return false;
  }
  return a.is_value32() ? a.value32() == b.value32()
                        : a.value64() == b.value64();
}

// Constant pool generation
// Pools are emitted in the instruction stream, preferably after unconditional
// jumps or after returns from functions (in dead code locations).
// If a long code sequence does not contain unconditional jumps, it is
// necessary to emit the constant pool before the pool gets too far from the
// location it is accessed from. In this case, we emit a jump over the emitted
// constant pool.
// Constants in the pool may be addresses of functions that gets relocated;
// if so, a relocation info entry is associated to the constant pool entry.
class ArmConstPool {
 public:
  explicit ArmConstPool(Assembler* assm);
  ~ArmConstPool();

  // Returns true when we need to write RelocInfo and false when we do not.
  bool RecordEntry(uint32_t data, RelocInfo::Mode rmode);
  bool RecordEntry(uint64_t data, RelocInfo::Mode rmode);

  size_t Entry32Count() const { return entry32_count_; }
  size_t Entry64Count() const { return entry64_count_; }
  bool IsEmpty() const { return entries_.empty(); }
  // Offset after which instructions using the pool will be out of range.
  bool IsInImmRangeIfEmittedAt(int pc_offset);
  // Size in bytes of the constant pool. Depending on parameters, the size will
  // include the branch over the pool and alignment padding.
  int ComputeSize(bool require_jump, bool require_alignment) const;
  bool IsAlignmentRequired(bool require_jump) const;
  // Emit the pool at the current pc with a branch over the pool if requested.
  void Emit(bool require_jump);
  bool ShouldEmitNow(bool require_jump) const;
  void Clear();
  bool IsBlocked() const;
  void BlockFor(int instructions);
  void SetNextCheckIn(size_t instructions);
  int NextCheckOffset() const { return next_check_; }
  int BlockedUntilOffset() const { return blocked_until_; }

  // Class for scoping postponing the constant pool generation.
  class BlockScope {
   public:
    explicit BlockScope(ArmConstPool* pool) : pool_(pool) {
      pool_->StartBlock();
    }
    ~BlockScope() { pool_->EndBlock(); }

   private:
    ArmConstPool* pool_;
    DISALLOW_IMPLICIT_CONSTRUCTORS(BlockScope);
  };
  void StartBlock();
  void EndBlock();

  // Hard limit to the const pool which must not be exceeded.
  static const size_t kMaxDistToPool32;
  static const size_t kMaxDistToPool64;
  // Approximate distance where the pool should be emitted.
  static const size_t kApproxDistToIntPool;
  static const size_t kApproxDistToFPPool;
  // Approximate distance where the pool should be emitted if
  // no jump is required.
  static const size_t kOpportunityDistToPool32;
  static const size_t kOpportunityDistToPool64;
  // PC distance between constant pool checks.
  static const size_t kCheckConstPoolInterval;
  // Number of entries in the pool which trigger a check.
  static const size_t kApproxMaxPoolEntryCount;

 private:
  void EmitEntries();
  void EmitMarker(bool require_alignment);
  int prologue_size(bool require_jump) const;
  bool RecordKey(ConstPoolKey key, int offset);
  bool IsDuplicate(const ConstPoolKey& key);
  void EmitGuard();
  void Emit(const ConstPoolKey& key);
  void SetLoadOffsetToConstPoolEntry(int load_offset, int entry_offset,
                                     const ConstPoolKey& key);

  Assembler* assm_;
  // Keep track of the first instruction requiring a constant pool entry
  // since the previous constant pool was emitted.
  int first_use_32_;
  int first_use_64_;
  std::multimap<ConstPoolKey, int> entries_;
  size_t entry32_count_;
  size_t entry64_count_;
  // Repeated checking whether the constant pool should be emitted is rather
  // expensive. By default we only check again once a number of instructions
  // has been generated. The next check will be performed at {next_check_}.
  int next_check_;
  // The next two variables are used to block the constant pool emission.
  int blocked_nesting_;
  int blocked_until_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CONSTANT_POOL_H_
