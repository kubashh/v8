// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_EXTERNAL_POINTER_TABLE_H_
#define V8_EXECUTION_EXTERNAL_POINTER_TABLE_H_

#include "src/utils/utils.h"

namespace v8 {
namespace internal {

static const int kExternalPointerTableInitialCapacity = 1024;

class V8_EXPORT_PRIVATE ExternalPointerTable {
 public:
  ExternalPointerTable()
      : buffer_(reinterpret_cast<Address*>(
            malloc(kExternalPointerTableInitialCapacity * sizeof(Address)))),
        length_(0),
        capacity_(kExternalPointerTableInitialCapacity),
        freelist_head_(0) {}

  ~ExternalPointerTable() { ::free(buffer_); }

  ExternalPointer_t get(uint32_t index) const {
    DCHECK(HAS_SMI_TAG(index));
    index >>= 1;
    CHECK(index < length_);
    return buffer_[index];
  }

  void set(uint32_t index, ExternalPointer_t value) {
    DCHECK(HAS_SMI_TAG(index));
    index >>= 1;
    CHECK(index < length_);
    buffer_[index] = value;
  }

  uint32_t allocate() {
    uint32_t idx = length_++;
    if (idx >= capacity_) {
      GrowTable(this);
    }
    return idx << 1;
  }

  void free(ExternalPointer_t index) {
    // TODO(saelo) implement simple free list here, i.e. set
    // buffer_[index] to freelist_head_ and set freelist_head
    // to index
  }

  uint32_t size() const { return length_; }

  static void GrowTable(ExternalPointerTable* table);

 private:
  friend class Isolate;

  Address* buffer_;
  uint32_t length_;
  uint32_t capacity_;
  uint32_t freelist_head_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_EXTERNAL_POINTER_TABLE_H_
