// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SECURITY_CAGED_POINTER_CONSTANTS_H_
#define V8_SECURITY_CAGED_POINTER_CONSTANTS_H_

#include "include/v8-internal.h"

namespace v8 {
namespace internal {

// Process-wide caged pointer constants. Initialized after the virtual memory
// cage has been initialized.
class CagedPointerConstants final {
 public:
  CagedPointer_t empty_backing_store_buffer() const {
    return empty_backing_store_buffer_;
  }
  Address empty_backing_store_buffer_address() const {
    return reinterpret_cast<Address>(&empty_backing_store_buffer_);
  }
  void set_empty_backing_store_buffer(CagedPointer_t value) {
    empty_backing_store_buffer_ = value;
  }

  void Reset() { empty_backing_store_buffer_ = 0; }

 private:
  // Empty ArrayBuffer backing store buffer.
  CagedPointer_t empty_backing_store_buffer_ = 0;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SECURITY_CAGED_POINTER_CONSTANTS_H_
