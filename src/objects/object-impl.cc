// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/object-impl.h"

#include <sstream>

#include "src/objects.h"
#include "src/ostreams.h"
#include "src/string-stream.h"

namespace v8 {
namespace internal {

template <HeapObjectReferenceType kRefType>
void ObjectImpl<kRefType>::ShortPrint(FILE* out) {
  OFStream os(out);
  os << Brief(*this);
}

template <HeapObjectReferenceType kRefType>
void ObjectImpl<kRefType>::ShortPrint(StringStream* accumulator) {
  std::ostringstream os;
  os << Brief(*this);
  accumulator->Add(os.str().c_str());
}

template <HeapObjectReferenceType kRefType>
void ObjectImpl<kRefType>::ShortPrint(std::ostream& os) {
  os << Brief(*this);
}

// Explicit instantiation declarations.
template class ObjectImpl<HeapObjectReferenceType::STRONG>;
template class ObjectImpl<HeapObjectReferenceType::WEAK>;

}  // namespace internal
}  // namespace v8
