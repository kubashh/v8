// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/bigint.h"

#include "src/objects-inl.h"

namespace v8 {
namespace internal {

bool BigInt::Equals(BigInt* other) { return value() == other->value(); }

void BigInt::BigIntPrint(std::ostream& os) {  // NOLINT
  HeapObject::PrintHeader(os, "BigInt");
  os << "- value: " << value();
  os << "\n";
}

}  // namespace internal
}  // namespace v8
