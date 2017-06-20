// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DATAFLOW_H_
#define V8_DATAFLOW_H_

#include "src/allocation.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class BitVector : public ZoneObject {
 public:
  union DataStorage {
    uintptr_t* ptr_;    // valid if data_length_ > 0
    uintptr_t inline_;  // valid if data_length_ == 0

    DataStorage(uintptr_t value) : inline_(value) {}
  };

  // Iterator for the elements of this BitVector.
  class Iterator BASE_EMBEDDED {
   public:
    explicit Iterator(BitVector* target)
        : target_(target),
          // Initialize current_index_ to -1 if data_length_ is 0, so that
          // incrementing it once will make it equal to data_length_ and make
          // Done() true
          current_index_(target_->data_length_ == 0 ? -1 : 0),
          current_value_(target->data_length_ == 0 ? target->storage_.inline_
                                                   : target->storage_.ptr_[0]),
          current_(-1) {
      Advance();
    }
    ~Iterator() {}

    bool Done() const { return current_index_ >= target_->data_length_; }
    void Advance();

    int Current() const {
      DCHECK(!Done());
      return current_;
    }

   private:
    uintptr_t SkipZeroBytes(uintptr_t val) {
      while ((val & 0xFF) == 0) {
        val >>= 8;
        current_ += 8;
      }
      return val;
    }
    uintptr_t SkipZeroBits(uintptr_t val) {
      while ((val & 0x1) == 0) {
        val >>= 1;
        current_++;
      }
      return val;
    }

    BitVector* target_;
    int current_index_;
    uintptr_t current_value_;
    int current_;

    friend class BitVector;
  };

  static const int kDataBits = kPointerSize * 8;
  static const int kDataBitShift = kPointerSize == 8 ? 6 : 5;
  static const uintptr_t kOne = 1;  // This saves some static_casts.

  explicit BitVector(int length)
      : length_(length), data_length_(0), storage_(0) {
    DCHECK_LE(0, length);
    DCHECK_LE(length, kDataBits);
    // Clearing is implicit
  }

  BitVector(int length, Zone* zone)
      : length_(length), data_length_(SizeFor(length)), storage_(0) {
    DCHECK_LE(0, length);
    if (data_length_ > 0) {
      storage_.ptr_ = zone->NewArray<uintptr_t>(data_length_);
      Clear();
    }
    // Otherwise, clearing is implicit
  }

  BitVector(const BitVector& other)
      : length_(other.length_),
        data_length_(other.data_length_),
        storage_(other.storage_.inline_) {
    DCHECK_LE(length_, kDataBits);
    DCHECK_EQ(data_length_, 0);
  }

  BitVector(const BitVector& other, Zone* zone)
      : length_(other.length_),
        data_length_(other.data_length_),
        storage_(other.storage_.inline_) {
    if (data_length_ > 0) {
      storage_.ptr_ = zone->NewArray<uintptr_t>(data_length_);
      for (int i = 0; i < other.data_length_; i++) {
        storage_.ptr_[i] = other.storage_.ptr_[i];
      }
    }
  }

  static int SizeFor(int length) {
    if (length <= kDataBits) return 0;
    return 1 + ((length - 1) / kDataBits);
  }

  void CopyFrom(const BitVector& other) {
    DCHECK_LE(other.length(), length());
    CopyFrom(other.storage_, other.data_length_);
  }

  void Resize(int new_length, Zone* zone) {
    DCHECK_GT(new_length, length());
    int new_data_length = SizeFor(new_length);
    if (new_data_length > data_length_) {
      DataStorage old_data = storage_;
      int old_data_length = data_length_;

      // Make sure the new data length is large enough to need allocation.
      DCHECK_GT(new_data_length, 0);
      storage_.ptr_ = zone->NewArray<uintptr_t>(new_data_length);
      data_length_ = new_data_length;
      CopyFrom(old_data, old_data_length);
    }
    length_ = new_length;
  }

  bool Contains(int i) const {
    DCHECK(i >= 0 && i < length());
    uintptr_t block =
        data_length_ == 0 ? storage_.inline_ : storage_.ptr_[i / kDataBits];
    return (block & (kOne << (i % kDataBits))) != 0;
  }

  void Add(int i) {
    DCHECK(i >= 0 && i < length());
    if (data_length_ == 0) {
      storage_.inline_ |= (kOne << i);
    } else {
      storage_.ptr_[i / kDataBits] |= (kOne << (i % kDataBits));
    }
  }

  void AddAll() {
    if (data_length_ == 0) {
      storage_.inline_ = -1;
    } else {
      memset(storage_.ptr_, -1, sizeof(uintptr_t) * data_length_);
    }
  }

  void Remove(int i) {
    DCHECK(i >= 0 && i < length());
    if (data_length_ == 0) {
      storage_.inline_ &= ~(kOne << i);
    } else {
      storage_.ptr_[i / kDataBits] &= ~(kOne << (i % kDataBits));
    }
  }

  void Union(const BitVector& other) {
    DCHECK(other.length() == length());
    if (data_length_ == 0) {
      DCHECK_EQ(other.data_length_, 0);
      storage_.inline_ |= other.storage_.inline_;
    } else {
      for (int i = 0; i < data_length_; i++) {
        storage_.ptr_[i] |= other.storage_.ptr_[i];
      }
    }
  }

  bool UnionIsChanged(const BitVector& other) {
    DCHECK(other.length() == length());
    if (data_length_ == 0) {
      DCHECK_EQ(other.data_length_, 0);
      uintptr_t old_data = storage_.inline_;
      storage_.inline_ |= other.storage_.inline_;
      return storage_.inline_ != old_data;
    } else {
      bool changed = false;
      for (int i = 0; i < data_length_; i++) {
        uintptr_t old_data = storage_.ptr_[i];
        storage_.ptr_[i] |= other.storage_.ptr_[i];
        if (storage_.ptr_[i] != old_data) changed = true;
      }
      return changed;
    }
  }

  void Intersect(const BitVector& other) {
    DCHECK(other.length() == length());
    if (data_length_ == 0) {
      DCHECK_EQ(other.data_length_, 0);
      storage_.inline_ &= other.storage_.inline_;
    } else {
      for (int i = 0; i < data_length_; i++) {
        storage_.ptr_[i] &= other.storage_.ptr_[i];
      }
    }
  }

  bool IntersectIsChanged(const BitVector& other) {
    DCHECK(other.length() == length());
    if (data_length_ == 0) {
      DCHECK_EQ(other.data_length_, 0);
      uintptr_t old_data = storage_.inline_;
      storage_.inline_ &= other.storage_.inline_;
      return storage_.inline_ != old_data;
    } else {
      bool changed = false;
      for (int i = 0; i < data_length_; i++) {
        uintptr_t old_data = storage_.ptr_[i];
        storage_.ptr_[i] &= other.storage_.ptr_[i];
        if (storage_.ptr_[i] != old_data) changed = true;
      }
      return changed;
    }
  }

  void Subtract(const BitVector& other) {
    DCHECK(other.length() == length());
    if (data_length_ == 0) {
      DCHECK_EQ(other.data_length_, 0);
      storage_.inline_ &= ~other.storage_.inline_;
    } else {
      for (int i = 0; i < data_length_; i++) {
        storage_.ptr_[i] &= ~other.storage_.ptr_[i];
      }
    }
  }

  void Clear() {
    if (data_length_ == 0) {
      storage_.inline_ = 0;
    } else {
      for (int i = 0; i < data_length_; i++) {
        storage_.ptr_[i] = 0;
      }
    }
  }

  bool IsEmpty() const {
    if (data_length_ == 0) {
      return storage_.inline_ == 0;
    } else {
      for (int i = 0; i < data_length_; i++) {
        if (storage_.ptr_[i] != 0) return false;
      }
      return true;
    }
  }

  bool Equals(const BitVector& other) const {
    DCHECK(other.length() == length());
    if (data_length_ == 0) {
      DCHECK_EQ(other.data_length_, 0);
      return storage_.inline_ == other.storage_.inline_;
    } else {
      for (int i = 0; i < data_length_; i++) {
        if (storage_.ptr_[i] != other.storage_.ptr_[i]) return false;
      }
      return true;
    }
  }

  int Count() const;

  int length() const { return length_; }

#ifdef DEBUG
  void Print();
#endif

 private:
  int length_;
  int data_length_;
  DataStorage storage_;

  void CopyFrom(DataStorage other_data, int other_data_length) {
    DCHECK_LE(other_data_length, data_length_);

    if (data_length_ == 0) {
      DCHECK_EQ(other_data_length, 0);
      storage_.inline_ = other_data.inline_;
    } else if (other_data_length == 0) {
      storage_.ptr_[0] = other_data.inline_;
      for (int i = 1; i < data_length_; i++) {
        storage_.ptr_[i] = 0;
      }
    } else {
      for (int i = 0; i < other_data_length; i++) {
        storage_.ptr_[i] = other_data.ptr_[i];
      }
      for (int i = other_data_length; i < data_length_; i++) {
        storage_.ptr_[i] = 0;
      }
    }
  }

  DISALLOW_ASSIGN(BitVector);
};


class GrowableBitVector BASE_EMBEDDED {
 public:
  class Iterator BASE_EMBEDDED {
   public:
    Iterator(const GrowableBitVector* target, Zone* zone)
        : it_(target->bits_ == nullptr ? new (zone) BitVector(1, zone)
                                       : target->bits_) {}
    bool Done() const { return it_.Done(); }
    void Advance() { it_.Advance(); }
    int Current() const { return it_.Current(); }

   private:
    BitVector::Iterator it_;
  };

  GrowableBitVector() : bits_(nullptr) {}
  GrowableBitVector(int length, Zone* zone)
      : bits_(new (zone) BitVector(length, zone)) {}

  bool Contains(int value) const {
    if (!InBitsRange(value)) return false;
    return bits_->Contains(value);
  }

  void Add(int value, Zone* zone) {
    EnsureCapacity(value, zone);
    bits_->Add(value);
  }

  void Union(const GrowableBitVector& other, Zone* zone) {
    for (Iterator it(&other, zone); !it.Done(); it.Advance()) {
      Add(it.Current(), zone);
    }
  }

  void Clear() {
    if (bits_ != nullptr) bits_->Clear();
  }

 private:
  static const int kInitialLength = 1024;

  bool InBitsRange(int value) const {
    return bits_ != nullptr && bits_->length() > value;
  }

  void EnsureCapacity(int value, Zone* zone) {
    if (InBitsRange(value)) return;
    int new_length = bits_ == nullptr ? kInitialLength : bits_->length();
    while (new_length <= value) new_length *= 2;

    if (bits_ == nullptr) {
      bits_ = new (zone) BitVector(new_length, zone);
    } else {
      bits_->Resize(new_length, zone);
    }
  }

  BitVector* bits_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DATAFLOW_H_
