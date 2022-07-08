// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_CHUNK_INL_H_
#define V8_HEAP_MEMORY_CHUNK_INL_H_

#include "src/heap/memory-chunk.h"
#include "src/heap/spaces-inl.h"

namespace v8 {
namespace internal {

void MemoryChunk::IncrementExternalBackingStoreBytes(
    ExternalBackingStoreType type, size_t amount) {
#ifndef V8_ENABLE_THIRD_PARTY_HEAP
  base::CheckedIncrement(&external_backing_store_bytes_[type], amount);
  owner()->IncrementExternalBackingStoreBytes(type, amount);
#endif
}

void MemoryChunk::DecrementExternalBackingStoreBytes(
    ExternalBackingStoreType type, size_t amount) {
#ifndef V8_ENABLE_THIRD_PARTY_HEAP
  base::CheckedDecrement(&external_backing_store_bytes_[type], amount);
  owner()->DecrementExternalBackingStoreBytes(type, amount);
#endif
}

void MemoryChunk::MoveExternalBackingStoreBytes(ExternalBackingStoreType type,
                                                MemoryChunk* from,
                                                MemoryChunk* to,
                                                size_t amount) {
  DCHECK_NOT_NULL(from->owner());
  DCHECK_NOT_NULL(to->owner());
  base::CheckedDecrement(&(from->external_backing_store_bytes_[type]), amount);
  base::CheckedIncrement(&(to->external_backing_store_bytes_[type]), amount);
  Space::MoveExternalBackingStoreBytes(type, from->owner(), to->owner(),
                                       amount);
}

AllocationSpace MemoryChunk::owner_identity() const {
  if (InReadOnlySpace()) return RO_SPACE;
  return owner()->identity();
}

MemoryChunk::iterator::iterator()
    : heap_object_()
#if V8_COMPRESS_POINTERS
      ,
      cage_base_(kNullAddress)
#endif  // V8_COMPRESS_POINTERS
{
}

MemoryChunk::iterator::iterator(const MemoryChunk* chunk, Address ptr)
    : heap_object_(HeapObject::FromAddress(ptr))
#if V8_COMPRESS_POINTERS
      ,
      cage_base_(chunk->heap()->isolate())
#endif  // V8_COMPRESS_POINTERS
{
  DCHECK_LE(chunk->area_start(), ptr);
  DCHECK_GE(chunk->area_end(), ptr);
}

MemoryChunk::iterator& MemoryChunk::iterator::operator++() {
  const int size = heap_object_.Size(cage_base());
  const Address next_ptr = heap_object_.address() + size;
  heap_object_ = HeapObject::FromAddress(next_ptr);
  return *this;
}

MemoryChunk::iterator MemoryChunk::iterator::operator++(int) {
  iterator temp(*this);
  ++(*this);
  return temp;
}

MemoryChunk::iterator MemoryChunk::begin() const {
  return iterator(this, area_start());
}

MemoryChunk::iterator MemoryChunk::begin(Address ptr) const {
  return iterator(this, ptr);
}

MemoryChunk::iterator MemoryChunk::end() const {
  return iterator(this, area_end());
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_CHUNK_INL_H_
