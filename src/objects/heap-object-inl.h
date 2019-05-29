// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_HEAP_OBJECT_INL_H_
#define V8_OBJECTS_HEAP_OBJECT_INL_H_

#include "src/objects/heap-object.h"

#include "src/heap/heap-write-barrier-inl.h"
// TODO(jkummerow): Get rid of this by moving NROSO::GetIsolate elsewhere.
#include "src/execution/isolate.h"
#include "src/ptr-compr-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

//
// StrongTaggedField<T, kFieldOffset> implementation.
//

// static
template <typename T, int kFieldOffset>
Address StrongTaggedField<T, kFieldOffset>::address(HeapObject host) {
  return host.address() + kFieldOffset;
}

// static
template <typename T, int kFieldOffset>
Tagged_t* StrongTaggedField<T, kFieldOffset>::location(HeapObject host) {
  return reinterpret_cast<Tagged_t*>(address(host));
}

// static
template <typename T, int kFieldOffset>
template <typename TOnHeapAddress>
Address StrongTaggedField<T, kFieldOffset>::tagged_to_full(
    TOnHeapAddress on_heap_addr, Tagged_t tagged_value) {
#ifdef V8_COMPRESS_POINTERS
  if (kIsSmi) {
    return DecompressTaggedSigned(tagged_value);
  } else if (kIsHeapObject) {
    return DecompressTaggedPointer(on_heap_addr, tagged_value);
  } else {
    return DecompressTaggedAny(on_heap_addr, tagged_value);
  }
#else
  return tagged_value;
#endif
}

// static
template <typename T, int kFieldOffset>
Tagged_t StrongTaggedField<T, kFieldOffset>::full_to_tagged(Address value) {
#ifdef V8_COMPRESS_POINTERS
  return CompressTagged(value);
#else
  return value;
#endif
}

// static
template <typename T, int kFieldOffset>
bool StrongTaggedField<T, kFieldOffset>::contains_value(HeapObject host,
                                                        Address raw_value) {
  Tagged_t value = *location(host);
  return value == static_cast<Tagged_t>(raw_value);
}

// // static
// template <typename T, int kFieldOffset>
// Address StrongTaggedField<T, kFieldOffset>::load_raw(HeapObject host) {
//   Tagged_t value = *location(host);
//   return tagged_to_full(host.ptr(), value);
// }

// // static
// template <typename T, int kFieldOffset>
// Address StrongTaggedField<T, kFieldOffset>::load_raw(ROOT_PARAM,
//                                                      HeapObject host) {
//   Tagged_t value = *location(host);
//   return tagged_to_full(ROOT_VALUE, value);
// }

// static
template <typename T, int kFieldOffset>
T StrongTaggedField<T, kFieldOffset>::load(HeapObject host) {
  Tagged_t value = *location(host);
  return T::cast(Object(tagged_to_full(host.ptr(), value)));
}

// static
template <typename T, int kFieldOffset>
T StrongTaggedField<T, kFieldOffset>::load(ROOT_PARAM, HeapObject host) {
  Tagged_t value = *location(host);
  return T::cast(Object(tagged_to_full(ROOT_VALUE, value)));
}

// static
template <typename T, int kFieldOffset>
void StrongTaggedField<T, kFieldOffset>::store(HeapObject host, T value) {
  *location(host) = full_to_tagged(value.ptr());
}

// static
template <typename T, int kFieldOffset>
T StrongTaggedField<T, kFieldOffset>::Relaxed_Load(HeapObject host) {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(location(host));
  return T::cast(Object(tagged_to_full(host.ptr(), value)));
}

// static
template <typename T, int kFieldOffset>
T StrongTaggedField<T, kFieldOffset>::Relaxed_Load(ROOT_PARAM,
                                                   HeapObject host) {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(location(host));
  return T::cast(Object(tagged_to_full(ROOT_VALUE, value)));
}

// static
template <typename T, int kFieldOffset>
void StrongTaggedField<T, kFieldOffset>::Relaxed_Store(HeapObject host,
                                                       T value) {
  AsAtomicTagged::Relaxed_Store(location(host), full_to_tagged(value.ptr()));
}

// static
template <typename T, int kFieldOffset>
T StrongTaggedField<T, kFieldOffset>::Acquire_Load(HeapObject host) {
  AtomicTagged_t value = AsAtomicTagged::Acquire_Load(location(host));
  return T::cast(Object(tagged_to_full(host.ptr(), value)));
}

// static
template <typename T, int kFieldOffset>
T StrongTaggedField<T, kFieldOffset>::Acquire_Load(ROOT_PARAM,
                                                   HeapObject host) {
  AtomicTagged_t value = AsAtomicTagged::Acquire_Load(location(host));
  return T::cast(Object(tagged_to_full(ROOT_VALUE, value)));
}

// static
template <typename T, int kFieldOffset>
void StrongTaggedField<T, kFieldOffset>::Release_Store(HeapObject host,
                                                       T value) {
  AsAtomicTagged::Release_Store(location(host), full_to_tagged(value.ptr()));
}

//   static inline T Release_CompareAndSwap(T old, T target);

// static
template <typename T, int kFieldOffset>
Address StrongTaggedField<T, kFieldOffset>::Relaxed_Load_raw(HeapObject host) {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(location(host));
  return tagged_to_full(host.ptr(), value);
}

// static
template <typename T, int kFieldOffset>
Address StrongTaggedField<T, kFieldOffset>::Relaxed_Load_raw(ROOT_PARAM,
                                                             HeapObject host) {
  AtomicTagged_t value = AsAtomicTagged::Relaxed_Load(location(host));
  return tagged_to_full(ROOT_VALUE, value);
}

// static
template <typename T, int kFieldOffset>
void StrongTaggedField<T, kFieldOffset>::Relaxed_Store_raw(HeapObject host,
                                                           Address value) {
  AsAtomicTagged::Relaxed_Store(location(host), full_to_tagged(value));
}

// static
template <typename T, int kFieldOffset>
Address StrongTaggedField<T, kFieldOffset>::Acquire_Load_raw(HeapObject host) {
  AtomicTagged_t value = AsAtomicTagged::Acquire_Load(location(host));
  return tagged_to_full(host.ptr(), value);
}

// static
template <typename T, int kFieldOffset>
Address StrongTaggedField<T, kFieldOffset>::Acquire_Load_raw(ROOT_PARAM,
                                                             HeapObject host) {
  AtomicTagged_t value = AsAtomicTagged::Acquire_Load(location(host));
  return tagged_to_full(ROOT_VALUE, value);
}

// static
template <typename T, int kFieldOffset>
void StrongTaggedField<T, kFieldOffset>::Release_Store_raw(HeapObject host,
                                                           Address value) {
  AsAtomicTagged::Release_Store(location(host), full_to_tagged(value));
}

//
// HeapObject
//

HeapObject::HeapObject(Address ptr, AllowInlineSmiStorage allow_smi)
    : Object(ptr) {
  SLOW_DCHECK(
      (allow_smi == AllowInlineSmiStorage::kAllowBeingASmi && IsSmi()) ||
      IsHeapObject());
}

// static
Heap* NeverReadOnlySpaceObject::GetHeap(const HeapObject object) {
  return GetHeapFromWritableObject(object);
}

// static
Isolate* NeverReadOnlySpaceObject::GetIsolate(const HeapObject object) {
  return GetIsolateFromWritableObject(object);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_HEAP_OBJECT_INL_H_
