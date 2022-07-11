// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_BASIC_MEMORY_CHUNK_INL_H_
#define V8_HEAP_BASIC_MEMORY_CHUNK_INL_H_

#include "src/heap/basic-memory-chunk.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

ChunkObjectRange::iterator::iterator()
    : heap_object_()
#if V8_COMPRESS_POINTERS
      ,
      cage_base_(kNullAddress)
#endif  // V8_COMPRESS_POINTERS
{
}

ChunkObjectRange::iterator::iterator(PtrComprCageBase cage_base, Address ptr)
    : heap_object_(HeapObject::FromAddress(ptr))
#if V8_COMPRESS_POINTERS
      ,
      cage_base_(cage_base)
#endif  // V8_COMPRESS_POINTERS
{
}

ChunkObjectRange::iterator& ChunkObjectRange::iterator::operator++() {
  const int size = heap_object_.Size(cage_base());
  const Address next_ptr = heap_object_.address() + size;
  heap_object_ = HeapObject::FromAddress(next_ptr);
  return *this;
}

ChunkObjectRange::iterator ChunkObjectRange::iterator::operator++(int) {
  iterator temp(*this);
  ++(*this);
  return temp;
}

ChunkObjectRange::ChunkObjectRange(PtrComprCageBase cage_base,
                                   const BasicMemoryChunk* chunk)
    : begin_(cage_base, chunk->area_start()),
      end_(cage_base, chunk->area_end()) {}

ChunkObjectRange::ChunkObjectRange(PtrComprCageBase cage_base,
                                   const BasicMemoryChunk* chunk, Address ptr)
    : begin_(cage_base, ptr), end_(cage_base, chunk->area_end()) {
  DCHECK_LE(chunk->area_start(), ptr);
  DCHECK_GE(chunk->area_end(), ptr);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_BASIC_MEMORY_CHUNK_INL_H_
