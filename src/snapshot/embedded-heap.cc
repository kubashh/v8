// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded-heap.h"

#include "src/common/checks.h"
#include "src/common/ptr-compr-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/objects/objects.h"
#include "src/utils/memcopy.h"

namespace v8 {
namespace internal {

void EmbeddedHeapWriter::WriteHeader(ReadOnlyHeap* ro_heap) {
  WriteValue(static_cast<int>(ro_heap->read_only_object_cache_size()));
  for (size_t i = 0; i < ro_heap->read_only_object_cache_size(); ++i) {
    const Address address =
        HeapObject::cast(ro_heap->cached_read_only_object(i)).address();
    WriteValue(CompressTagged(address));
  }
  WriteValue(ReadOnlyHeap::kEntriesCount);
  for (size_t i = 0; i < ReadOnlyHeap::kEntriesCount; i++) {
    // FIXME:
    WriteValue(0);
  }
}

void EmbeddedHeapWriter::WriteValue(Tagged_t value) {
  // We shouldn't be using this if this fails.
  DCHECK(sizeof(Tagged_t) == sizeof(uint32_t));
  sink_->push_back(value & 0xFF);
  sink_->push_back((value >> 8) & 0xFF);
  sink_->push_back((value >> 16) & 0xFF);
  sink_->push_back((value >> 24) & 0xFF);
}

void EmbeddedHeapReader::ReadHeader(Isolate* isolate) {
  size_t object_cache_size = ReadValue();
  for (size_t i = 0; i < object_cache_size; i++) {
    Address address = DecompressTaggedPointer(isolate, ReadValue());
    USE(address);
  }
  size_t root_count = ReadValue();
  for (size_t i = 0; i < root_count; i++) {
    CHECK(ReadValue() == 0);
  }
}

Tagged_t EmbeddedHeapReader::ReadValue() {
  // We shouldn't be using this if this fails.
  DCHECK(sizeof(Tagged_t) == sizeof(uint32_t));
  return ReadByte() | (ReadByte() << 8) | (ReadByte() << 16) |
         (ReadByte() << 24);
}

}  // namespace internal
}  // namespace v8
