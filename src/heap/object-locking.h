// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_OBJECT_LOCKING_H_
#define V8_HEAP_OBJECT_LOCKING_H_

#include "src/heap/marking.h"
#include "src/heap/spaces.h"
#include "src/objects.h"

namespace v8 {
namespace internal {

class ObjectLocking {
 public:
  V8_INLINE static void Lock(HeapObject* object);
  V8_INLINE static void Unlock(HeapObject* object);

 private:
  V8_INLINE static MarkBit MarkBitFrom(HeapObject* obj);
  V8_INLINE static MarkBit MarkBitFrom(MemoryChunk* p, Address addr);
};

inline MarkBit ObjectLocking::MarkBitFrom(MemoryChunk* p, Address addr) {
#ifdef V8_MARKING_LOCK_PER_PAGE
  return MarkBit(reinterpret_cast<MarkBit::CellType*>(&(p->marking_lock_)),
                 MarkBit::CellType{1} << (sizeof(MarkBit::CellType) - 1));
#else
  return p->lock_bitmap()->MarkBitFromIndex(p->AddressToMarkbitIndex(addr));
#endif  // V8_MARKING_LOCK_PER_PAGE
}

inline MarkBit ObjectLocking::MarkBitFrom(HeapObject* obj) {
  return MarkBitFrom(MemoryChunk::FromAddress(obj->address()), obj->address());
}

inline void ObjectLocking::Lock(HeapObject* object) {
  Locking::Lock(MarkBitFrom(object));
}

inline void ObjectLocking::Unlock(HeapObject* object) {
  Locking::Unlock(MarkBitFrom(object));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_OBJECT_LOCKING_H_
