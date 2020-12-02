// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/aligned-slot-allocator.h"

#include "src/base/bits.h"
#include "src/base/logging.h"

namespace v8 {
namespace internal {

namespace {}  // namespace

int AlignedSlotAllocator::NextSlot(int size) const {
  if (size <= 1 && isValid(next1_)) return next1_;
  if (size <= 2 && isValid(next2_)) return next2_;
  if (size <= 4) return next4_;
  UNREACHABLE();
}

int AlignedSlotAllocator::Allocate(int size) {
  // |next4_| is 4-aligned, |next2_| is 2-aligned.
  DCHECK_EQ(0, next4_ & 3);
  DCHECK_IMPLIES(isValid(next2_), (next2_ & 1) == 0);
  int result = kInvalidSlot;
  switch (size) {
    case 1: {
      if (isValid(next1_)) {
        result = next1_;
        next1_ = kInvalidSlot;
      } else if (isValid(next2_)) {
        result = next2_;
        next1_ = result + 1;
        next2_ = kInvalidSlot;
      } else {
        result = next4_;
        next1_ = result + 1;
        next2_ = result + 2;
        next4_ += 4;
      }
      break;
    }
    case 2: {
      if (isValid(next2_)) {
        result = next2_;
        next2_ = kInvalidSlot;
      } else {
        result = next4_;
        next2_ = result + 2;
        next4_ += 4;
      }
      break;
    }
    case 4: {
      result = next4_;
      next4_ += 4;
      break;
    }
    default: {
      // Other sizes must be reserved, since we can't align them.
      return Reserve(size);
    }
  }
  end_ = std::max(end_, result + size);
  return result;
}

int AlignedSlotAllocator::Reserve(int size) {
  // Reserve slots at |end_| and invalidate fragments below the new |end_|.
  int result = end_;
  end_ += size;
  switch (end_ & 3) {
    case 0: {
      next1_ = next2_ = kInvalidSlot;
      next4_ = end_;
      break;
    }
    case 1: {
      next1_ = end_;
      next2_ = end_ + 1;
      next4_ = end_ + 3;
      break;
    }
    case 2: {
      next1_ = kInvalidSlot;
      next2_ = end_;
      next4_ = end_ + 2;
      break;
    }
    case 3: {
      next1_ = end_;
      next2_ = kInvalidSlot;
      next4_ = end_ + 1;
      break;
    }
  }
  return result;
}

int AlignedSlotAllocator::Align(int size) {
  DCHECK(base::bits::IsPowerOfTwo(size));
  int misalignment = size - (end_ & (size - 1));
  return Reserve(misalignment);
}

}  // namespace internal
}  // namespace v8
