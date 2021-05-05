// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_HEAP_NUMBER_INL_H_
#define V8_OBJECTS_HEAP_NUMBER_INL_H_

#include "src/objects/heap-number.h"

#include "src/objects/objects-inl.h"
#include "src/objects/primitive-heap-object-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/heap-number-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(HeapNumber)

uint64_t HeapNumber::value_as_bits() const {
  // Bug(v8:8875): HeapNumber's double may be unaligned.
  return base::ReadUnalignedValue<uint64_t>(field_address(kValueOffset));
}

uint64_t HeapNumber::value_as_bits_relaxed() const {
  constexpr size_t kNumWords = sizeof(uint64_t) / sizeof(uint32_t);
  CHECK_EQ(kNumWords, 2);
  uint32_t words[kNumWords];
  for (size_t word = 0; word < kNumWords; ++word) {
    words[word] = reinterpret_cast<std::atomic<uint32_t>*>(
                      field_address(kValueOffset + word * sizeof(uint32_t)))
                      ->load(std::memory_order_relaxed);
  }

  uint64_t output;
  CHECK_EQ(sizeof(words), sizeof(output));
  memcpy(&output, words, sizeof(output));
  return output;
}

void HeapNumber::set_value_as_bits(uint64_t bits) {
  base::WriteUnalignedValue<uint64_t>(field_address(kValueOffset), bits);
}

int HeapNumber::get_exponent() {
  return ((ReadField<int>(kExponentOffset) & kExponentMask) >> kExponentShift) -
         kExponentBias;
}

int HeapNumber::get_sign() {
  return ReadField<int>(kExponentOffset) & kSignMask;
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_HEAP_NUMBER_INL_H_
