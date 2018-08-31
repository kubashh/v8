// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PREPARSED_SCOPE_DATA_INL_H_
#define V8_PARSING_PREPARSED_SCOPE_DATA_INL_H_

#include "src/parsing/preparsed-scope-data.h"

#include "src/assert-scope.h"

namespace v8 {
namespace internal {

// Classes which are internal to prepared-scope-data.cc, but are exposed in
// a header for tests.

#ifdef DEBUG
constexpr int kMagicValue = 0xC0DE0DE;

constexpr size_t kUint32Size = 5;
constexpr size_t kUint8Size = 2;
constexpr size_t kQuarterMarker = 0;
constexpr size_t kPlaceholderSize = kUint32Size;
#else
constexpr size_t kUint32Size = 4;
constexpr size_t kUint8Size = 1;
constexpr size_t kPlaceholderSize = 0;
#endif

const size_t kSkippableFunctionDataSize = 4 * kUint32Size + 1 * kUint8Size;

template <class Data>
class BaseConsumedPreParsedScopeData : public ConsumedPreParsedScopeData {
 public:
  class ByteData {
   public:
    ByteData()
        : data_(nullptr), index_(0), stored_quarters_(0), stored_byte_(0) {}

    // Reading from the ByteData is only allowed when a ReadingScope is on the
    // stack. This ensures that we have a DisallowHeapAllocation in place
    // whenever ByteData holds a raw pointer into the heap.
    class ReadingScope {
     public:
      ReadingScope(ByteData* consumed_data, Data* data)
          : consumed_data_(consumed_data) {
        consumed_data->data_ = data;
      }
      explicit ReadingScope(BaseConsumedPreParsedScopeData<Data>* parent)
          : ReadingScope(parent->scope_data_.get(), parent->GetScopeData()) {}
      ~ReadingScope() { consumed_data_->data_ = nullptr; }

     private:
      ByteData* consumed_data_;
      DisallowHeapAllocation no_gc;
    };

    void SetPosition(int position) { index_ = position; }

    size_t RemainingBytes() const {
      DCHECK_NOT_NULL(data_);
      return data_->length() - index_;
    }

    int32_t ReadUint32() {
      DCHECK_NOT_NULL(data_);
      DCHECK_GE(RemainingBytes(), kUint32Size);
#ifdef DEBUG
      // Check that there indeed is an integer following.
      DCHECK_EQ(data_->get(index_++), kUint32Size);
#endif
      int32_t result = 0;
      byte* p = reinterpret_cast<byte*>(&result);
      for (int i = 0; i < 4; ++i) {
        *p++ = data_->get(index_++);
      }
      stored_quarters_ = 0;
      return result;
    }

    uint8_t ReadUint8() {
      DCHECK_NOT_NULL(data_);
      DCHECK_GE(RemainingBytes(), kUint8Size);
#ifdef DEBUG
      // Check that there indeed is a byte following.
      DCHECK_EQ(data_->get(index_++), kUint8Size);
#endif
      stored_quarters_ = 0;
      return data_->get(index_++);
    }

    uint8_t ReadQuarter() {
      DCHECK_NOT_NULL(data_);
      if (stored_quarters_ == 0) {
        DCHECK_GE(RemainingBytes(), kUint8Size);
#ifdef DEBUG
        // Check that there indeed are quarters following.
        DCHECK_EQ(data_->get(index_++), kQuarterMarker);
#endif
        stored_byte_ = data_->get(index_++);
        stored_quarters_ = 4;
      }
      // Read the first 2 bits from stored_byte_.
      uint8_t result = (stored_byte_ >> 6) & 3;
      DCHECK_LE(result, 3);
      --stored_quarters_;
      stored_byte_ <<= 2;
      return result;
    }

   private:
    Data* data_;
    int index_;
    uint8_t stored_quarters_;
    uint8_t stored_byte_;
  };

  BaseConsumedPreParsedScopeData()
      : scope_data_(new ByteData()), child_index_(0) {}

  virtual Data* GetScopeData() = 0;

  virtual ProducedPreParsedScopeData* GetChildData(Zone* zone,
                                                   int child_index) = 0;

  ProducedPreParsedScopeData* GetDataForSkippableFunction(
      Zone* zone, int start_position, int* end_position, int* num_parameters,
      int* num_inner_functions, bool* uses_super_property,
      LanguageMode* language_mode) final;

  void RestoreScopeAllocationData(DeclarationScope* scope) final;

#ifdef DEBUG
  void VerifyDataStart();
#endif

 private:
  void RestoreData(Scope* scope);
  void RestoreDataForVariable(Variable* var);
  void RestoreDataForInnerScopes(Scope* scope);

  std::unique_ptr<ByteData> scope_data_;
  // When consuming the data, these indexes point to the data we're going to
  // consume next.
  int child_index_;

  DISALLOW_COPY_AND_ASSIGN(BaseConsumedPreParsedScopeData);
};

// Implementation of ConsumedPreParsedScopeData for on-heap data.
class OnHeapConsumedPreParsedScopeData final
    : public BaseConsumedPreParsedScopeData<PodArray<uint8_t>> {
 public:
  OnHeapConsumedPreParsedScopeData(Isolate* isolate,
                                   Handle<PreParsedScopeData> data);

  PodArray<uint8_t>* GetScopeData() final;
  ProducedPreParsedScopeData* GetChildData(Zone* zone, int child_index) final;

 private:
  Isolate* isolate_;
  Handle<PreParsedScopeData> data_;
};

// Wraps a ZoneVector<uint8_t> to have with functions named the same as
// PodArray<uint8_t>.
class ZoneVectorWrapper {
 public:
  explicit ZoneVectorWrapper(ZoneVector<uint8_t>* data) : data_(data) {}

  int length() const { return static_cast<int>(data_->size()); }

  uint8_t get(int index) const { return data_->at(index); }

 private:
  ZoneVector<uint8_t>* data_;

  DISALLOW_COPY_AND_ASSIGN(ZoneVectorWrapper);
};

// A serialized PreParsedScopeData in zone memory (as apposed to being on-heap).
class ZonePreParsedScopeData : public ZoneObject {
 public:
  ZonePreParsedScopeData(Zone* zone,
                         ZoneChunkList<uint8_t>::iterator byte_data_begin,
                         ZoneChunkList<uint8_t>::iterator byte_data_end,
                         int child_length);

  Handle<PreParsedScopeData> Serialize(Isolate* isolate);

  int child_length() const { return static_cast<int>(children_.size()); }

  ZonePreParsedScopeData* get_child(int index) const {
    return children_[index];
  }

  void set_child(int index, ZonePreParsedScopeData* child) {
    children_[index] = child;
  }

  ZoneVector<uint8_t>* byte_data() { return &byte_data_; }

 private:
  ZoneVector<uint8_t> byte_data_;
  ZoneVector<ZonePreParsedScopeData*> children_;

  DISALLOW_COPY_AND_ASSIGN(ZonePreParsedScopeData);
};

// Implementation of ConsumedPreParsedScopeData for zone data produced directly
// from a ProducedPreParsedScopeData.
class ZoneConsumedPreParsedScopeData final
    : public BaseConsumedPreParsedScopeData<ZoneVectorWrapper> {
 public:
  ZoneConsumedPreParsedScopeData(Zone* zone, ProducedPreParsedScopeData* data);

  ZoneVectorWrapper* GetScopeData() final;
  ProducedPreParsedScopeData* GetChildData(Zone* zone, int child_index) final;

 private:
  ZonePreParsedScopeData* data_;
  ZoneVectorWrapper scope_data_wrapper_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_PREPARSED_SCOPE_DATA_INL_H_
