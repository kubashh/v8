// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OBJECT_IMPL_INL_H_
#define V8_OBJECTS_OBJECT_IMPL_INL_H_

#include "src/objects/object-impl.h"

#ifdef V8_COMPRESS_POINTERS
#include "src/isolate.h"
#endif
#include "src/objects/heap-object.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

template <HeapObjectReferenceType kRefType>
bool ObjectImpl<kRefType>::ToSmi(Smi* value) {
  if (HAS_SMI_TAG(ptr_)) {
    *value = Smi::cast(Object(ptr_));
    return true;
  }
  return false;
}

template <HeapObjectReferenceType kRefType>
Smi ObjectImpl<kRefType>::ToSmi() const {
  DCHECK(HAS_SMI_TAG(ptr_));
  return Smi::cast(Object(ptr_));
}

template <HeapObjectReferenceType kRefType>
constexpr bool ObjectImpl<kRefType>::IsCleared() const {
  return kCanBeWeak &&
         (static_cast<uint32_t>(ptr_) == kClearedWeakHeapObjectLower32);
}

template <HeapObjectReferenceType kRefType>
constexpr bool ObjectImpl<kRefType>::IsStrongOrWeak() const {
  if (IsSmi() || IsCleared()) {
    return false;
  }
  return true;
}

template <HeapObjectReferenceType kRefType>
bool ObjectImpl<kRefType>::GetHeapObject(HeapObject* result) const {
  if (IsSmi() || IsCleared()) {
    return false;
  }
  *result = GetHeapObject();
  return true;
}

template <HeapObjectReferenceType kRefType>
bool ObjectImpl<kRefType>::GetHeapObject(
    HeapObject* result, HeapObjectReferenceType* reference_type) const {
  if (IsSmi() || IsCleared()) {
    return false;
  }
  *reference_type = IsWeakOrCleared() ? HeapObjectReferenceType::WEAK
                                      : HeapObjectReferenceType::STRONG;
  *result = GetHeapObject();
  return true;
}

template <HeapObjectReferenceType kRefType>
bool ObjectImpl<kRefType>::GetHeapObjectIfStrong(HeapObject* result) const {
  if (IsStrong()) {
    *result = HeapObject::cast(Object(ptr_));
    return true;
  }
  return false;
}

template <HeapObjectReferenceType kRefType>
HeapObject ObjectImpl<kRefType>::GetHeapObjectAssumeStrong() const {
  DCHECK(IsStrong());
  return HeapObject::cast(Object(ptr_));
}

template <HeapObjectReferenceType kRefType>
constexpr bool ObjectImpl<kRefType>::IsWeak() const {
  return kCanBeWeak && HAS_WEAK_HEAP_OBJECT_TAG(ptr_) && !IsCleared();
}

template <HeapObjectReferenceType kRefType>
bool ObjectImpl<kRefType>::GetHeapObjectIfWeak(HeapObject* result) const {
  if (kCanBeWeak) {
    if (IsWeak()) {
      *result = GetHeapObject();
      return true;
    }
    return false;
  } else {
    DCHECK(!HAS_WEAK_HEAP_OBJECT_TAG(ptr_));
    return false;
  }
}

template <HeapObjectReferenceType kRefType>
HeapObject ObjectImpl<kRefType>::GetHeapObjectAssumeWeak() const {
  DCHECK(IsWeak());
  return GetHeapObject();
}

template <HeapObjectReferenceType kRefType>
HeapObject ObjectImpl<kRefType>::GetHeapObject() const {
  DCHECK(!IsSmi());
  if (kCanBeWeak) {
    DCHECK(!IsCleared());
    return HeapObject::cast(Object(ptr_ & ~kWeakHeapObjectMask));
  } else {
    DCHECK(!HAS_WEAK_HEAP_OBJECT_TAG(ptr_));
    return HeapObject::cast(Object(ptr_));
  }
}

template <HeapObjectReferenceType kRefType>
Object ObjectImpl<kRefType>::GetHeapObjectOrSmi() const {
  if (IsSmi()) {
    return Object(ptr_);
  }
  return GetHeapObject();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_OBJECT_IMPL_INL_H_
