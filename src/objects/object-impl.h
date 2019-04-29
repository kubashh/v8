// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OBJECT_IMPL_H_
#define V8_OBJECTS_OBJECT_IMPL_H_

#include "include/v8-internal.h"
#include "include/v8.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

// An ObjectImpl is a base class for Object (which is either a Smi or a strong
// reference to a HeapObject) and MaybeObject (which is either a Smi, a strong
// reference to a HeapObject, a weak reference to a HeapObject, or a cleared
// weak reference.
// This class provides storage and various predicates that check Smi and heap
// object tags' values.
template <HeapObjectReferenceType kRefType>
class ObjectImpl {
 public:
  static const bool kCanBeWeak = kRefType == HeapObjectReferenceType::WEAK;

  constexpr ObjectImpl() : ptr_(kNullAddress) {}
  explicit constexpr ObjectImpl(Address ptr) : ptr_(ptr) {}

  // Make clang on Linux catch what MSVC complains about on Windows:
  operator bool() const = delete;

  constexpr bool operator==(ObjectImpl other) const {
    return ptr_ == other.ptr_;
  }
  constexpr bool operator!=(ObjectImpl other) const {
    return ptr_ != other.ptr_;
  }

  // For using in std::set and std::map.
  constexpr bool operator<(ObjectImpl other) const {
    return ptr_ < other.ptr();
  }

  constexpr Address ptr() const { return ptr_; }

  constexpr inline bool IsObject() const { return !IsWeakOrCleared(); }

  constexpr bool IsSmi() const { return HAS_SMI_TAG(ptr_); }
  inline bool ToSmi(Smi* value);
  inline Smi ToSmi() const;

  constexpr inline bool IsHeapObject() const {
    if (kCanBeWeak) {
      return IsStrong();
    } else {
      DCHECK_EQ(!IsSmi(), IsStrong());
      return !IsSmi();
    }
  }

  constexpr inline bool IsCleared() const;

  constexpr inline bool IsStrongOrWeak() const;
  constexpr inline bool IsStrong() const {
    return HAS_STRONG_HEAP_OBJECT_TAG(ptr_);
  }

  // If this MaybeObject is a strong pointer to a HeapObject, returns true and
  // sets *result. Otherwise returns false.
  inline bool GetHeapObjectIfStrong(HeapObject* result) const;

  // DCHECKs that this MaybeObject is a strong pointer to a HeapObject and
  // returns the HeapObject.
  inline HeapObject GetHeapObjectAssumeStrong() const;

  constexpr inline bool IsWeak() const;
  constexpr inline bool IsWeakOrCleared() const {
    return kCanBeWeak && HAS_WEAK_HEAP_OBJECT_TAG(ptr_);
  }

  // If this MaybeObject is a weak pointer to a HeapObject, returns true and
  // sets *result. Otherwise returns false.
  inline bool GetHeapObjectIfWeak(HeapObject* result) const;

  // DCHECKs that this MaybeObject is a weak pointer to a HeapObject and
  // returns the HeapObject.
  inline HeapObject GetHeapObjectAssumeWeak() const;

  // If this MaybeObject is a strong or weak pointer to a HeapObject, returns
  // true and sets *result. Otherwise returns false.
  inline bool GetHeapObject(HeapObject* result) const;
  inline bool GetHeapObject(HeapObject* result,
                            HeapObjectReferenceType* reference_type) const;

  // DCHECKs that this MaybeObject is a strong or a weak pointer to a HeapObject
  // and returns the HeapObject.
  inline HeapObject GetHeapObject() const;

  // DCHECKs that this MaybeObject is a strong or a weak pointer to a HeapObject
  // or a SMI and returns the HeapObject or SMI.
  inline Object GetHeapObjectOrSmi() const;

  template <typename T>
  T cast() const {
    DCHECK(!HAS_WEAK_HEAP_OBJECT_TAG(ptr_));
    return T::cast(Object(ptr_));
  }

  // Prints this object without details.
  void ShortPrint(FILE* out = stdout);

  // Prints this object without details to a message accumulator.
  void ShortPrint(StringStream* accumulator);

  void ShortPrint(std::ostream& os);

#ifdef OBJECT_PRINT
  void Print();
  void Print(std::ostream& os);
#else
  void Print() { ShortPrint(); }
  void Print(std::ostream& os) { ShortPrint(os); }
#endif

 private:
  friend class CompressedObjectSlot;
  friend class FullObjectSlot;

  Address ptr_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_OBJECT_IMPL_H_
