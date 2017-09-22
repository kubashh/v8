// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_SERIALIZER_INL_H_
#define V8_SNAPSHOT_SERIALIZER_INL_H_

#include "src/objects-inl.h"
#include "src/snapshot/serializer.h"

namespace v8 {
namespace internal {

void Serializer::HandleExternalReferenceRedirections(HeapObject* obj) {
  if (isolate_->external_reference_redirector() && obj->IsAccessorInfo()) {
    // Wipe external reference redirects in the accessor info.
    AccessorInfo* info = AccessorInfo::cast(obj);
    Address original_address = Foreign::cast(info->getter())->foreign_address();
    Foreign::cast(info->js_getter())->set_foreign_address(original_address);
    accessor_infos_.push_back(info);
  }
}

Serializer::ObjectSerializer::ObjectSerializer(Serializer* serializer,
                                               HeapObject* obj,
                                               SnapshotByteSink* sink,
                                               HowToCode how_to_code,
                                               WhereToPoint where_to_point)
    : serializer_(serializer),
      object_(obj),
      sink_(sink),
      reference_representation_(how_to_code + where_to_point),
      bytes_processed_so_far_(0),
      code_has_been_output_(false) {
#ifdef DEBUG
  serializer_->PushStack(obj);
#endif  // DEBUG
  serializer_->HandleExternalReferenceRedirections(obj);
}

Serializer::ObjectSerializer::~ObjectSerializer() {
#ifdef DEBUG
  serializer_->PopStack();
#endif  // DEBUG
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_SERIALIZER_INL_H_
