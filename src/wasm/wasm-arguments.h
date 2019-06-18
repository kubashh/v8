// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_WASM_ARGUMENTS_H_
#define V8_WASM_WASM_ARGUMENTS_H_

#include <stdint.h>
#include <vector>

#include "src/common/globals.h"
#include "src/common/v8memory.h"
#include "src/wasm/value-type.h"

namespace v8 {
namespace internal {
namespace wasm {

class ArgumentsPacker {
 public:
  explicit ArgumentsPacker(size_t buffer_size)
      : heap_buffer_(buffer_size <= kMaxOnStackBuffer ? 0 : buffer_size),
        buffer_((buffer_size <= kMaxOnStackBuffer) ? on_stack_buffer_
                                                   : heap_buffer_.data()) {}
  i::Address argv() const { return reinterpret_cast<i::Address>(buffer_); }
  void Reset() { offset_ = 0; }

  template <typename T>
  void Push(T val) {
    Address address = reinterpret_cast<Address>(buffer_ + offset_);
    offset_ += sizeof(val);
    WriteUnalignedValue(address, val);
  }

  template <typename T>
  T Pop() {
    Address address = reinterpret_cast<Address>(buffer_ + offset_);
    offset_ += sizeof(T);
    return ReadUnalignedValue<T>(address);
  }

  static int TotalSize(FunctionSig* sig) {
    int return_size = 0;
    for (ValueType t : sig->returns()) {
      return_size += ValueTypes::ElementSizeInBytes(t);
    }
    int param_size = 0;
    for (ValueType t : sig->parameters()) {
      param_size += ValueTypes::ElementSizeInBytes(t);
    }
    return std::max(return_size, param_size);
  }

 private:
  static const size_t kMaxOnStackBuffer = 10 * i::kSystemPointerSize;

  uint8_t on_stack_buffer_[kMaxOnStackBuffer];
  std::vector<uint8_t> heap_buffer_;
  uint8_t* buffer_;
  size_t offset_ = 0;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_ARGUMENTS_H_
