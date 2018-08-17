// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CONSTANT_POOL_H_
#define V8_CONSTANT_POOL_H_

#include "src/assembler.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

class ConstantPoolKey {
 public:
  explicit ConstantPoolKey(uint64_t value,
                           RelocInfo::Mode rmode = RelocInfo::NONE)
      : is_value32_(false), value64_(value), rmode_(rmode) {}

  explicit ConstantPoolKey(uint32_t value,
                           RelocInfo::Mode rmode = RelocInfo::NONE)
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
    // CODE_TARGETs can be shared because they aren't patched anymore,
    // and we make sure we emit only one reloc info for them (thus delta
    // patching) will apply the delta only once. At the moment, we do not dedup
    // code targets if they are wrapped in a heap object request (value == 0).
    bool is_sharable_code_target =
        rmode_ == RelocInfo::CODE_TARGET &&
        (is_value32() ? (value32() != 0) : (value64() != 0));
    return RelocInfo::IsShareableRelocMode(rmode_) || is_sharable_code_target;
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
inline bool operator<(const ConstantPoolKey& a, const ConstantPoolKey& b) {
  if (a.is_value32() < b.is_value32()) return true;
  if (a.is_value32() > b.is_value32()) return false;
  if (a.rmode() < b.rmode()) return true;
  if (a.rmode() > b.rmode()) return false;
  return a.is_value32() ? a.value32() < b.value32() : a.value64() < b.value64();
}

inline bool operator==(const ConstantPoolKey& a, const ConstantPoolKey& b) {
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
class ConstantPool {
 public:
  explicit ConstantPool(Assembler* assm);
  ~ConstantPool();

  // Returns true when we need to write RelocInfo and false when we do not.
  bool RecordEntry(uint32_t data, RelocInfo::Mode rmode);
  bool RecordEntry(uint64_t data, RelocInfo::Mode rmode);

  size_t Entry32Count() const { return entry32_count_; }
  size_t Entry64Count() const { return entry64_count_; }
  bool IsEmpty() const { return entries_.empty(); }
  // Check if pool will be out of range at {pc_offset}.
  bool IsInImmRangeIfEmittedAt(int pc_offset);
  // Size in bytes of the constant pool. Depending on parameters, the size will
  // include the branch over the pool and alignment padding.
  int ComputeSize(bool require_jump, bool require_alignment) const;

  // Emit the pool at the current pc with a branch over the pool if requested.
  void Emit(bool require_jump);
  bool ShouldEmitNow(bool require_jump) const;
  void Clear();

  // Constant pool emisssion can be blocked temporarily.
  bool IsBlocked() const;
  void BlockFor(int instructions);

  // Repeated checking whether the constant pool should be emitted is expensive;
  // only check once a number of instructions have been generated.
  void SetNextCheckIn(size_t instructions);
  int NextCheckOffset() const { return next_check_; }
  int BlockedUntilOffset() const { return blocked_until_; }

  // Class for scoping postponing the constant pool generation.
  class BlockScope {
   public:
    explicit BlockScope(ConstantPool* pool) : pool_(pool) {
      pool_->StartBlock();
    }
    ~BlockScope() { pool_->EndBlock(); }

   private:
    ConstantPool* pool_;
    DISALLOW_IMPLICIT_CONSTRUCTORS(BlockScope);
  };
  void StartBlock();
  void EndBlock();

  // Hard limit to the const pool which must not be exceeded.
  static const size_t kMaxDistToPool32;
  static const size_t kMaxDistToPool64;
  // Approximate distance where the pool should be emitted.
  static const size_t kApproxDistToPool32;
  static const size_t kApproxDistToPool64;
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
  int PrologueSize(bool require_jump) const;
  bool RecordKey(ConstantPoolKey key, int offset);
  bool IsDuplicate(const ConstantPoolKey& key);
  void EmitGuard();
  void Emit(const ConstantPoolKey& key);
  void SetLoadOffsetToConstPoolEntry(int load_offset, int entry_offset,
                                     const ConstantPoolKey& key);
  bool IsAlignmentRequiredIfEmittedAt(bool require_jump, int pc_offset) const;

  Assembler* assm_;
  // Keep track of the first instruction requiring a constant pool entry
  // since the previous constant pool was emitted.
  int first_use_32_ = -1;
  int first_use_64_ = -1;
  std::multimap<ConstantPoolKey, int> entries_;
  size_t entry32_count_ = 0;
  size_t entry64_count_ = 0;
  int next_check_ = 0;
  int blocked_nesting_ = 0;
  int blocked_until_ = 0;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CONSTANT_POOL_H_
